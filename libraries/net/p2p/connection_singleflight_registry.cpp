module;

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
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
import forge.net.p2p.identity;

#include "details/connection_singleflight_registry.hxx"

namespace forge::net::p2p::detail {

connection_singleflight_registry::connection_singleflight_registry(test_hooks test_hooks) noexcept
    : test_hooks_(test_hooks) {}

connection_singleflight_registry::lease::lease(peer_id peer, std::shared_ptr<entry> owner,
                                               std::shared_ptr<completion_channel> completion, bool queued)
    : peer_(std::move(peer)), owner_(std::move(owner)), completion_(std::move(completion)), queued_(queued) {}

boost::asio::awaitable<connection_singleflight_registry::outcome> connection_singleflight_registry::lease::wait() {
   if (!completion_) {
      co_return outcome{
          .error = exceptions::code::internal,
          .message = "invalid P2P connection singleflight participant",
      };
   }
   co_return co_await completion_->async_receive(boost::asio::use_awaitable);
}

connection_singleflight_registry::operation::operation(peer_id peer, std::shared_ptr<entry> owner)
    : peer_(std::move(peer)), owner_(std::move(owner)) {}

connection_singleflight_registry::joined connection_singleflight_registry::join(const peer_id& peer,
                                                                                boost::asio::any_io_executor executor,
                                                                                std::size_t maximum_waiters) {
   if (closed_) {
      return joined{.status = join_status::closed};
   }
   auto start = std::optional<operation>{};
   auto queued = false;
   auto new_entry = false;
   auto current = std::shared_ptr<entry>{};
   if (const auto found = entries_.find(peer); found != entries_.end()) {
      current = found->second;
      if (queued_participants_ >= maximum_waiters) {
         return joined{.status = join_status::backpressure};
      }
      queued = true;
   } else {
      current = std::make_shared<entry>();
      new_entry = true;
      start.emplace(operation{peer, current});
   }

   auto completed_result = std::optional<outcome>{};
   if (current->completed) {
      completed_result.emplace(current->result);
   }
   auto completion = std::make_shared<lease::completion_channel>(std::move(executor), 1);
   auto participant = lease{peer, current, completion, queued};
   auto completions = current->completions;
   prune_completions(completions);
   completions.push_back(completion);

   reach_test_failpoint(new_entry ? test_stage::before_new_entry_publish : test_stage::before_existing_entry_commit);
   if (completed_result) {
      static_cast<void>(completion->try_send(boost::system::error_code{}, std::move(*completed_result)));
   }

   current->completions.swap(completions);
   ++current->participants;
   if (queued) {
      ++queued_participants_;
   }
   if (new_entry) {
      // This is the only potentially throwing commit after all participant state is prepared.
      entries_.emplace(peer, current);
   }
   return joined{
       .status = join_status::accepted,
       .participant = std::move(participant),
       .start = std::move(start),
   };
}

void connection_singleflight_registry::complete(entry& owner, outcome result) noexcept {
   if (owner.completed) {
      return;
   }
   owner.result = std::move(result);
   owner.completed = true;
   prune_completions(owner);
   for (auto& pending : owner.completions) {
      if (auto completion = pending.lock()) {
         static_cast<void>(completion->try_send(boost::system::error_code{}, owner.result));
      }
   }
}

void connection_singleflight_registry::prune_completions(entry& owner) noexcept {
   prune_completions(owner.completions);
}

void connection_singleflight_registry::prune_completions(
    std::vector<std::weak_ptr<lease::completion_channel>>& completions) noexcept {
   std::erase_if(completions, [](const auto& completion) { return completion.expired(); });
}

void connection_singleflight_registry::reach_test_failpoint(test_stage stage) const {
   if (test_hooks_.reach != nullptr) {
      test_hooks_.reach(test_hooks_.context, stage);
   }
}

void connection_singleflight_registry::erase_if_unused(const peer_id& peer,
                                                       const std::shared_ptr<entry>& owner) noexcept {
   const auto found = entries_.find(peer);
   if (!owner->operation_active && owner->participants == 0 && found != entries_.end() && found->second == owner) {
      entries_.erase(found);
   }
}

void connection_singleflight_registry::finish(operation& active, outcome result) noexcept {
   if (!active.owner_) {
      return;
   }
   auto owner = std::move(active.owner_);
   complete(*owner, std::move(result));
   owner->operation_active = false;
   erase_if_unused(active.peer_, owner);
}

void connection_singleflight_registry::succeed(operation& active) noexcept {
   finish(active, outcome{.succeeded = true});
}

void connection_singleflight_registry::fail(operation& active, exceptions::code error, std::string message) noexcept {
   finish(active, outcome{.error = error, .message = std::move(message)});
}

void connection_singleflight_registry::leave(lease& participant) noexcept {
   if (!participant.owner_) {
      return;
   }
   auto owner = std::move(participant.owner_);
   participant.completion_.reset();
   if (owner->participants != 0) {
      --owner->participants;
   }
   if (participant.queued_ && queued_participants_ != 0) {
      --queued_participants_;
   }
   participant.queued_ = false;
   prune_completions(*owner);
   erase_if_unused(participant.peer_, owner);
}

void connection_singleflight_registry::close() noexcept {
   closed_ = true;
   for (const auto& [_, value] : entries_) {
      complete(*value, outcome{
                           .error = exceptions::code::closed,
                           .message = "P2P node stopped during connection singleflight",
                       });
      value->operation_active = false;
   }
   entries_.clear();
}

std::size_t connection_singleflight_registry::size() const noexcept {
   return entries_.size();
}

} // namespace forge::net::p2p::detail
