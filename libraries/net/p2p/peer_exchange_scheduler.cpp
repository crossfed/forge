module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.net.p2p.node;

import forge.net.p2p.exceptions;
import forge.net.p2p.identify;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;

#include "details/peer_exchange_scheduler.hxx"

namespace forge::net::p2p::detail {
namespace {

[[nodiscard]] bool session_less(const peer_exchange_scheduler::session& left,
                                const peer_exchange_scheduler::session& right) noexcept {
   if (left.peer != right.peer) {
      return left.peer < right.peer;
   }
   return left.session_id < right.session_id;
}

[[nodiscard]] peer_exchange_scheduler::clock::time_point
retry_deadline(peer_exchange_scheduler::clock::time_point now, std::chrono::milliseconds delay) noexcept {
   if (delay.count() <= 0 || now == peer_exchange_scheduler::clock::time_point::max()) {
      return now;
   }
   const auto remaining = peer_exchange_scheduler::clock::time_point::max() - now;
   const auto requested = std::chrono::duration_cast<peer_exchange_scheduler::clock::duration>(delay);
   return requested >= remaining ? peer_exchange_scheduler::clock::time_point::max() : now + requested;
}

} // namespace

bool peer_exchange_scheduler::claim::started() const noexcept {
   return status == claim_status::started;
}

bool peer_exchange_scheduler::eligible(const session& value) noexcept {
   return value.identify_state == identify::state::identified &&
          std::ranges::find(value.protocols, builtins::peer_exchange) != value.protocols.end();
}

peer_exchange_scheduler::peer_exchange_scheduler(std::size_t maximum_waiters) noexcept
    : maximum_waiters_{maximum_waiters} {}

std::vector<peer_exchange_scheduler::session>
peer_exchange_scheduler::normalized(const std::vector<session>& candidates) {
   auto result = candidates;
   std::ranges::sort(result, session_less);
   result.erase(std::unique(result.begin(), result.end(), [](const auto& left, const auto& right) {
                   return left.peer == right.peer;
                }),
                result.end());
   return result;
}

peer_exchange_scheduler::claim peer_exchange_scheduler::begin(const session& selected,
                                                               boost::asio::any_io_executor executor) {
   if (closed_) {
      return {.status = claim_status::closed, .selected = selected};
   }
   auto joined = operations_.join(selected.peer, std::move(executor), maximum_waiters_);
   if (joined.status == connection_singleflight_registry::join_status::closed) {
      return {.status = claim_status::closed, .selected = selected};
   }
   if (joined.status == connection_singleflight_registry::join_status::backpressure) {
      return {.status = claim_status::backpressure, .selected = selected};
   }
   auto [found, inserted] = entries_.try_emplace(selected.peer, entry{.active = true});
   if (!inserted) {
      found->second.active = true;
      found->second.retry_after = {};
   }
   return claim{
       .status = joined.start ? claim_status::started : claim_status::joined,
       .selected = selected,
       .participant = std::move(joined.participant),
       .start = std::move(joined.start),
   };
}

void peer_exchange_scheduler::expire(clock::time_point now) noexcept {
   for (auto current = entries_.begin(); current != entries_.end();) {
      if (!current->second.active && current->second.retry_after <= now) {
         current = entries_.erase(current);
      } else {
         ++current;
      }
   }
}

peer_exchange_scheduler::claim peer_exchange_scheduler::claim_next(const std::vector<session>& candidates,
                                                                     clock::time_point now, std::size_t limit,
                                                                     boost::asio::any_io_executor executor) {
   if (closed_) {
      return {.status = claim_status::closed};
   }
   if (limit == 0) {
      return {};
   }
   expire(now);
   for (const auto& candidate : normalized(candidates)) {
      if (!eligible(candidate)) {
         continue;
      }
      const auto current = entries_.find(candidate.peer);
      if (current != entries_.end()) {
         continue;
      }
      if (entries_.size() >= limit) {
         return {};
      }
      return begin(candidate, std::move(executor));
   }
   return {};
}

std::vector<peer_exchange_scheduler::claim>
peer_exchange_scheduler::claim_batch(const std::vector<session>& candidates, clock::time_point now,
                                     std::size_t state_limit, std::size_t batch_limit,
                                     boost::asio::any_io_executor executor) {
   auto result = std::vector<claim>{};
   if (closed_) {
      return result;
   }
   if (state_limit == 0 || batch_limit == 0) {
      return result;
   }

   expire(now);
   result.reserve(batch_limit);
   for (const auto& candidate : normalized(candidates)) {
      if (!eligible(candidate)) {
         continue;
      }

      if (const auto current = entries_.find(candidate.peer); current != entries_.end()) {
         if (!current->second.active) {
            continue;
         }
         auto joined = operations_.join(candidate.peer, executor, maximum_waiters_);
         if (joined.status != connection_singleflight_registry::join_status::accepted) {
            if (joined.status == connection_singleflight_registry::join_status::closed) {
               break;
            }
            continue;
         }
         result.push_back(claim{
             .status = joined.start ? claim_status::started : claim_status::joined,
             .selected = candidate,
             .participant = std::move(joined.participant),
             .start = std::move(joined.start),
         });
      } else if (entries_.size() < state_limit) {
         result.push_back(begin(candidate, executor));
      }

      if (result.size() == batch_limit) {
         break;
      }
   }
   return result;
}

peer_exchange_scheduler::claim peer_exchange_scheduler::claim_peer(const peer_id& peer,
                                                                     const std::vector<session>& candidates,
                                                                     clock::time_point now, std::size_t limit,
                                                                     boost::asio::any_io_executor executor) {
   if (closed_) {
      return {.status = claim_status::closed};
   }
   if (limit == 0) {
      return {.status = claim_status::not_selected};
   }
   expire(now);
   const auto ordered = normalized(candidates);
   const auto selected = std::ranges::find_if(ordered, [&](const auto& candidate) { return candidate.peer == peer; });
   if (selected == ordered.end() || !eligible(*selected)) {
      return {.status = claim_status::unavailable};
   }

   if (const auto current = entries_.find(peer); current != entries_.end()) {
      if (current->second.active) {
         auto joined = operations_.join(peer, std::move(executor), maximum_waiters_);
         if (joined.status == connection_singleflight_registry::join_status::closed) {
            return {.status = claim_status::closed, .selected = *selected};
         }
         if (joined.status == connection_singleflight_registry::join_status::backpressure) {
            return {.status = claim_status::backpressure, .selected = *selected};
         }
         return claim{
             .status = joined.start ? claim_status::started : claim_status::joined,
             .selected = *selected,
             .participant = std::move(joined.participant),
             .start = std::move(joined.start),
         };
      }
      return {.status = claim_status::backoff, .selected = *selected};
   }

   auto eligible_peers = std::size_t{};
   for (const auto& candidate : ordered) {
      if (!eligible(candidate)) {
         continue;
      }
      ++eligible_peers;
      if (candidate.peer == peer) {
         break;
      }
   }
   if (eligible_peers > limit || entries_.size() >= limit) {
      return {.status = claim_status::not_selected, .selected = *selected};
   }
   return begin(*selected, std::move(executor));
}

void peer_exchange_scheduler::complete(claim& active, connection_singleflight_registry::outcome outcome,
                                       clock::time_point now, std::chrono::milliseconds retry_after) noexcept {
   if (!active.start) {
      return;
   }
   if (const auto current = entries_.find(active.selected.peer); current != entries_.end()) {
      current->second.active = false;
      current->second.retry_after = retry_deadline(now, retry_after);
   }
   if (outcome.succeeded) {
      operations_.succeed(*active.start);
   } else {
      operations_.fail(*active.start, outcome.error.value_or(exceptions::code::internal), std::move(outcome.message));
   }
   active.start.reset();
}

void peer_exchange_scheduler::succeed(claim& active, clock::time_point now,
                                      std::chrono::milliseconds retry_after) noexcept {
   complete(active, connection_singleflight_registry::outcome{.succeeded = true}, now, retry_after);
}

void peer_exchange_scheduler::fail(claim& active, exceptions::code error, std::string message, clock::time_point now,
                                   std::chrono::milliseconds retry_after) noexcept {
   complete(active, connection_singleflight_registry::outcome{.error = error, .message = std::move(message)}, now,
            retry_after);
}

void peer_exchange_scheduler::leave(claim& participant) noexcept {
   operations_.leave(participant.participant);
}

void peer_exchange_scheduler::close() noexcept {
   closed_ = true;
   operations_.close();
   entries_.clear();
}

std::size_t peer_exchange_scheduler::size() const noexcept {
   return entries_.size();
}

} // namespace forge::net::p2p::detail
