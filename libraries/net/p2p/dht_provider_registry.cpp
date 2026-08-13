module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/this_coro.hpp>

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.asio.exceptions;
import forge.asio.gate;
import forge.asio.notification;
import forge.net.p2p.dht;
import forge.net.p2p.exceptions;
import forge.net.p2p.protocol;

#include "details/dht_provider_registry.hxx"
#include "details/lifecycle_wakeup.hxx"

namespace forge::net::p2p::detail {
namespace {

[[nodiscard]] std::uint64_t registration_hash(const protocol_id& protocol, const dht::key& key,
                                              std::uint64_t salt) noexcept {
   auto value = std::uint64_t{1469598103934665603ULL} ^ salt;
   const auto mix = [&value](std::uint8_t byte) {
      value ^= byte;
      value *= 1099511628211ULL;
   };
   for (const auto byte : protocol.value) {
      mix(static_cast<std::uint8_t>(byte));
   }
   for (const auto byte : key.bytes) {
      mix(byte);
   }
   return value;
}

[[noreturn]] void throw_registry_closed() {
   FORGE_THROW_EXCEPTION(exceptions::closed, "P2P DHT provider admission is closed");
}

[[noreturn]] void throw_registry_not_ready() {
   FORGE_THROW_EXCEPTION(exceptions::closed, "P2P DHT provider admission is not ready before hydration");
}

} // namespace

dht_provider_registry::dht_provider_registry(callbacks callbacks_value)
    : callbacks_{std::move(callbacks_value)}, admission_{std::make_shared<forge::asio::gate>()},
      changed_{std::make_shared<lifecycle_wakeup>()} {
   if (!callbacks_.track || !callbacks_.launch || !callbacks_.prepare || !callbacks_.publish || !callbacks_.remove ||
       !callbacks_.publication_limit) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P DHT provider registry callbacks are incomplete");
   }
}

dht_provider_registry::~dht_provider_registry() {
   seal();
}

dht_provider_registry::lease::lease(std::shared_ptr<owner_state> owner_value) : owner_{std::move(owner_value)} {}

std::size_t dht_provider_registry::profile_entry_count_locked(const protocol_id& protocol) const {
   return static_cast<std::size_t>(
       std::ranges::count_if(entries_, [&](const auto& item) { return item.first.protocol == protocol; }));
}

std::shared_ptr<dht_provider_registry::owner_state>
dht_provider_registry::add_owner_locked(const std::shared_ptr<entry>& value) {
   if (next_owner_id_ == (std::numeric_limits<std::uint64_t>::max)()) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P DHT provider owner sequence is exhausted");
   }
   auto owner = std::make_shared<owner_state>();
   owner->id = next_owner_id_++;
   owner->registration = value->registration;
   owner->changed = std::make_shared<lifecycle_wakeup>();
   value->owners.emplace(owner->id, owner);
   return owner;
}

boost::asio::awaitable<dht_provider_registry::lease>
dht_provider_registry::async_acquire(protocol_id protocol, dht::key key, dht::query_options query, schedule renewal) {
   auto operation = callbacks_.track();
   if (!operation) {
      throw_registry_closed();
   }

   auto admission = forge::asio::gate::ticket{};
   try {
      admission = co_await admission_->acquire();
   } catch (const forge::asio::exceptions::rejected&) {
      throw_registry_closed();
   }

   auto registration = registration_key{.protocol = std::move(protocol), .key = std::move(key)};
   auto initial_endpoint_generation = std::uint64_t{};
   while (true) {
      auto observed = changed_->epoch();
      auto existing = std::shared_ptr<entry>{};
      auto merged = query;
      auto wait_for_removal = false;
      auto retry_removal = false;
      {
         const auto lock = std::scoped_lock{mutex_};
         if (sealed_) {
            throw_registry_closed();
         }
         if (!admission_open_) {
            throw_registry_not_ready();
         }
         const auto found = entries_.find(registration);
         if (found == entries_.end()) {
            if (profile_entry_count_locked(registration.protocol) >= max_entries_per_profile) {
               FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected,
                                     "P2P DHT provider registry reached its per-profile bound");
            }
            ++admissions_in_flight_;
            initial_endpoint_generation = endpoint_generation_;
            break;
         }
         existing = found->second;
         if (existing->stop_requested) {
            if (existing->removal_failed && !existing->removal_in_flight) {
               existing->removal_failed = false;
               existing->removal_failure = {};
               existing->removal_in_flight = true;
               retry_removal = true;
            } else {
               wait_for_removal = true;
            }
         } else {
            merged.requested_count = std::max(existing->query.requested_count, query.requested_count);
            merged.quorum = std::max(existing->query.quorum, query.quorum);
            merged.timeout = std::max(existing->query.timeout, query.timeout);
            if (merged.requested_count == existing->query.requested_count && merged.quorum == existing->query.quorum &&
                merged.timeout == existing->query.timeout) {
               co_return lease{add_owner_locked(existing)};
            }
         }
      }

      if (retry_removal) {
         reset_owners_for_retry(existing);
         co_await async_remove(existing);
         auto retry_failure = std::exception_ptr{};
         {
            const auto lock = std::scoped_lock{mutex_};
            const auto found = entries_.find(registration);
            if (found != entries_.end() && found->second == existing && existing->removal_failed) {
               retry_failure = existing->removal_failure;
            }
         }
         if (retry_failure) {
            std::rethrow_exception(retry_failure);
         }
         continue;
      }

      if (wait_for_removal) {
         static_cast<void>(co_await changed_->async_wait(observed));
         continue;
      }

      auto provider = co_await callbacks_.prepare(registration.protocol, registration.key, existing->renewal);
      const auto published =
          co_await async_publish(registration.protocol, registration.key, std::move(provider), merged);
      if (published < merged.quorum) {
         FORGE_THROW_EXCEPTION(exceptions::peer_not_found,
                               "DHT provide did not reach the strengthened requested quorum");
      }
      {
         const auto lock = std::scoped_lock{mutex_};
         if (sealed_) {
            throw_registry_closed();
         }
         const auto found = entries_.find(registration);
         if (found == entries_.end() || found->second != existing || existing->stop_requested) {
            continue;
         }
         existing->query = merged;
         co_return lease{add_owner_locked(existing)};
      }
   }

   auto admission_pending = true;
   auto persist_attempted = false;
   auto value = std::make_shared<entry>();
   value->registration = registration;
   value->query = query;
   value->renewal = renewal;
   auto owner = std::shared_ptr<owner_state>{};
   auto failure = std::exception_ptr{};
   try {
      persist_attempted = true;
      auto provider = co_await callbacks_.prepare(registration.protocol, registration.key, renewal);
      const auto published =
          co_await async_publish(registration.protocol, registration.key, std::move(provider), query);
      if (published < query.quorum) {
         FORGE_THROW_EXCEPTION(exceptions::peer_not_found, "DHT provide did not reach the requested quorum");
      }

      {
         const auto lock = std::scoped_lock{mutex_};
         if (sealed_) {
            throw_registry_closed();
         }
         value->observed_endpoint_generation = initial_endpoint_generation;
         value->next_republish = std::chrono::steady_clock::now() + republish_delay(*value);
         owner = add_owner_locked(value);
         const auto [_, inserted] = entries_.emplace(value->registration, value);
         if (!inserted) {
            FORGE_THROW_EXCEPTION(exceptions::internal, "P2P DHT provider registration generation collided");
         }
      }
      changed_->notify();

      auto self = shared_from_this();
      if (!callbacks_.launch([self = std::move(self), value]() mutable -> boost::asio::awaitable<void> {
             co_await self->async_run(std::move(value));
          })) {
         throw_registry_closed();
      }
      {
         const auto lock = std::scoped_lock{mutex_};
         --admissions_in_flight_;
         admission_pending = false;
      }
      changed_->notify();
      co_return lease{owner};
   } catch (...) {
      failure = std::current_exception();
      if (value) {
         const auto lock = std::scoped_lock{mutex_};
         const auto found = entries_.find(value->registration);
         if (found != entries_.end() && found->second == value) {
            entries_.erase(found);
         }
      }
   }

   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   if (persist_attempted) {
      try {
         co_await async_rollback(registration);
      } catch (...) {
         failure = std::current_exception();
         const auto lock = std::scoped_lock{mutex_};
         value->owners.clear();
         value->stop_requested = true;
         value->removal_in_flight = false;
         value->removal_failed = true;
         value->removal_failure = failure;
         entries_.try_emplace(value->registration, value);
      }
   }
   {
      if (admission_pending) {
         const auto lock = std::scoped_lock{mutex_};
         --admissions_in_flight_;
         admission_pending = false;
      }
   }
   changed_->notify();
   if (owner) {
      finish_owner(owner, failure);
   }
   std::rethrow_exception(failure);
}

bool dht_provider_registry::active(const lease& value) const noexcept {
   if (!value.owner_) {
      return false;
   }
   const auto lock = std::scoped_lock{value.owner_->mutex};
   return !value.owner_->release_requested && !value.owner_->terminal;
}

void dht_provider_registry::request_release(const lease& value) noexcept {
   if (value.owner_) {
      request_release_owner(value.owner_);
   }
}

boost::asio::awaitable<void> dht_provider_registry::async_release(const lease& value) {
   if (value.owner_) {
      co_await async_release_owner(value.owner_);
   }
}

void dht_provider_registry::request_release_owner(const std::shared_ptr<owner_state>& owner) noexcept {
   try {
      {
         const auto lock = std::scoped_lock{owner->mutex};
         if (owner->release_requested || owner->terminal) {
            return;
         }
         owner->release_requested = true;
      }

      auto complete_now = false;
      {
         const auto lock = std::scoped_lock{mutex_};
         const auto found = entries_.find(owner->registration);
         if (found == entries_.end()) {
            complete_now = true;
         } else {
            auto& value = *found->second;
            for (auto current = value.owners.begin(); current != value.owners.end();) {
               if (current->second.expired() && current->first != owner->id) {
                  current = value.owners.erase(current);
               } else {
                  ++current;
               }
            }
            if (value.stop_requested) {
               complete_now = false;
            } else if (value.owners.size() > 1) {
               value.owners.erase(owner->id);
               complete_now = true;
            } else {
               value.stop_requested = true;
            }
         }
      }
      if (complete_now) {
         finish_owner(owner, {});
      }
      changed_->notify();
   } catch (...) {
      finish_owner(owner, std::current_exception());
   }
}

boost::asio::awaitable<void> dht_provider_registry::async_release_owner(const std::shared_ptr<owner_state>& owner) {
   request_release_owner(owner);
   auto retry = std::shared_ptr<entry>{};
   {
      auto failed = false;
      {
         const auto lock = std::scoped_lock{owner->mutex};
         failed = owner->terminal && static_cast<bool>(owner->terminal_failure);
      }
      if (failed) {
         const auto lock = std::scoped_lock{mutex_};
         const auto found = entries_.find(owner->registration);
         if (found != entries_.end() && found->second->removal_failed && !found->second->removal_in_flight) {
            retry = found->second;
            retry->removal_in_flight = true;
            retry->removal_failed = false;
            retry->removal_failure = {};
         }
      }
   }
   if (retry) {
      reset_owners_for_retry(retry);
      co_await async_remove(retry);
   }
   co_await async_wait_owner(owner);
}

boost::asio::awaitable<void> dht_provider_registry::async_wait_owner(const std::shared_ptr<owner_state>& owner) {
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   auto observed = owner->changed->epoch();
   while (true) {
      auto failure = std::exception_ptr{};
      {
         const auto lock = std::scoped_lock{owner->mutex};
         if (!owner->terminal) {
            failure = {};
         } else {
            failure = owner->terminal_failure;
            if (failure) {
               std::rethrow_exception(failure);
            }
            co_return;
         }
      }
      observed = co_await owner->changed->async_wait(observed);
   }
}

void dht_provider_registry::finish_owner(const std::shared_ptr<owner_state>& owner,
                                         std::exception_ptr failure) noexcept {
   try {
      {
         const auto lock = std::scoped_lock{owner->mutex};
         if (owner->terminal) {
            return;
         }
         owner->terminal = true;
         owner->terminal_failure = std::move(failure);
      }
      owner->changed->notify();
   } catch (...) {
   }
}

boost::asio::awaitable<void> dht_provider_registry::async_rollback(const registration_key& registration) {
   co_await callbacks_.remove(registration.protocol, registration.key);
}

boost::asio::awaitable<std::size_t> dht_provider_registry::async_publish(protocol_id protocol, dht::key key,
                                                                         dht::peer provider, dht::query_options query) {
   auto observed = changed_->epoch();
   while (true) {
      {
         const auto lock = std::scoped_lock{mutex_};
         if (sealed_) {
            throw_registry_closed();
         }
         const auto limit = callbacks_.publication_limit(protocol);
         if (limit == 0) {
            FORGE_THROW_EXCEPTION(exceptions::internal, "P2P DHT provider publication limit is zero");
         }
         auto& active = active_publications_[protocol];
         if (active < limit) {
            ++active;
            break;
         }
      }
      observed = co_await changed_->async_wait(observed);
   }

   try {
      const auto published = co_await callbacks_.publish(protocol, std::move(key), std::move(provider), query);
      release_publication(protocol);
      co_return published;
   } catch (...) {
      release_publication(protocol);
      throw;
   }
}

void dht_provider_registry::release_publication(const protocol_id& protocol) noexcept {
   try {
      {
         const auto lock = std::scoped_lock{mutex_};
         const auto found = active_publications_.find(protocol);
         if (found == active_publications_.end() || found->second == 0) {
            return;
         }
         if (--found->second == 0) {
            active_publications_.erase(found);
         }
      }
      changed_->notify();
   } catch (...) {
   }
}

boost::asio::awaitable<void> dht_provider_registry::async_remove(const std::shared_ptr<entry>& value) {
   auto failure = std::exception_ptr{};
   try {
      co_await callbacks_.remove(value->registration.protocol, value->registration.key);
   } catch (...) {
      failure = std::current_exception();
   }
   finish_entry(value, failure);
}

void dht_provider_registry::reset_owners_for_retry(const std::shared_ptr<entry>& value) noexcept {
   try {
      const auto registry_lock = std::scoped_lock{mutex_};
      const auto found = entries_.find(value->registration);
      if (found == entries_.end() || found->second != value || !value->removal_in_flight) {
         return;
      }
      for (const auto& [_, weak] : value->owners) {
         if (const auto owner = weak.lock()) {
            const auto owner_lock = std::scoped_lock{owner->mutex};
            owner->terminal = false;
            owner->terminal_failure = {};
         }
      }
   } catch (...) {
   }
}

void dht_provider_registry::finish_entry(const std::shared_ptr<entry>& value, std::exception_ptr failure) noexcept {
   try {
      auto owners = std::vector<std::shared_ptr<owner_state>>{};
      auto retain_for_retry = false;
      {
         const auto lock = std::scoped_lock{mutex_};
         const auto found = entries_.find(value->registration);
         if (found == entries_.end() || found->second != value) {
            return;
         }
         owners.reserve(value->owners.size());
         for (const auto& [_, weak] : value->owners) {
            if (const auto owner = weak.lock()) {
               owners.push_back(owner);
            }
         }
         value->stop_requested = true;
         value->removal_in_flight = false;
         retain_for_retry = static_cast<bool>(failure);
         value->removal_failed = retain_for_retry;
         value->removal_failure = failure;
         if (retain_for_retry && sealed_) {
            if (!drain_failure_) {
               drain_failure_ = failure;
            }
         }
      }
      for (const auto& owner : owners) {
         finish_owner(owner, failure);
      }
      if (!retain_for_retry) {
         const auto lock = std::scoped_lock{mutex_};
         const auto found = entries_.find(value->registration);
         if (found != entries_.end() && found->second == value) {
            entries_.erase(found);
         }
      }
      changed_->notify();
   } catch (...) {
      changed_->notify();
   }
}

std::chrono::milliseconds dht_provider_registry::republish_delay(const entry& value) const noexcept {
   const auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(value.renewal.republish_interval);
   const auto span = std::max(std::chrono::milliseconds{1}, interval / 20);
   const auto width = static_cast<std::uint64_t>(span.count()) * 2U + 1U;
   const auto offset =
       static_cast<std::int64_t>(
           registration_hash(value.registration.protocol, value.registration.key, 0x72657075626c6973ULL) % width) -
       span.count();
   return interval + std::chrono::milliseconds{offset};
}

std::chrono::milliseconds dht_provider_registry::retry_delay(const entry& value) const noexcept {
   const auto exponent = std::min<std::uint32_t>(value.publish_failures > 0 ? value.publish_failures - 1U : 0U, 9U);
   const auto base = std::chrono::seconds{std::uint64_t{1} << exponent};
   const auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(value.renewal.republish_interval);
   const auto cap =
       std::max(std::chrono::seconds{1},
                std::min(std::chrono::seconds{600}, std::chrono::duration_cast<std::chrono::seconds>(interval / 4)));
   const auto bounded = std::min(base, cap);
   const auto jitter_bound = std::max<std::int64_t>(1, bounded.count() / 5);
   const auto jitter = registration_hash(value.registration.protocol, value.registration.key,
                                         static_cast<std::uint64_t>(value.publish_failures)) %
                       static_cast<std::uint64_t>(jitter_bound + 1);
   return std::chrono::duration_cast<std::chrono::milliseconds>(bounded + std::chrono::seconds{jitter});
}

boost::asio::awaitable<void> dht_provider_registry::async_run(std::shared_ptr<entry> value) {
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   auto failure = std::exception_ptr{};
   try {
      while (true) {
         auto observed = changed_->epoch();
         auto deadline = std::chrono::steady_clock::time_point{};
         auto remove = false;
         auto republish = false;
         auto attempt_generation = std::uint64_t{};
         auto query = dht::query_options{};
         {
            const auto lock = std::scoped_lock{mutex_};
            remove = sealed_ || value->stop_requested;
            if (remove) {
               value->removal_in_flight = true;
               value->removal_failed = false;
               value->removal_failure = {};
            }
            attempt_generation = endpoint_generation_;
            republish = !remove && (value->observed_endpoint_generation != endpoint_generation_ ||
                                    value->next_republish <= std::chrono::steady_clock::now());
            deadline = value->next_republish;
            query = value->query;
         }
         if (remove) {
            co_await async_remove(value);
            co_return;
         }
         if (!republish) {
            static_cast<void>(co_await changed_->async_wait_until(observed, deadline));
            continue;
         }

         auto failure = std::exception_ptr{};
         try {
            auto provider =
                co_await callbacks_.prepare(value->registration.protocol, value->registration.key, value->renewal);
            const auto published = co_await async_publish(value->registration.protocol, value->registration.key,
                                                          std::move(provider), query);
            if (published < query.quorum) {
               FORGE_THROW_EXCEPTION(exceptions::peer_not_found, "DHT provider republish did not reach quorum");
            }
         } catch (...) {
            failure = std::current_exception();
         }

         {
            const auto lock = std::scoped_lock{mutex_};
            value->observed_endpoint_generation = attempt_generation;
            if (failure) {
               value->publish_failures = std::min<std::uint32_t>(value->publish_failures + 1U, 10U);
               value->next_republish = std::chrono::steady_clock::now() + retry_delay(*value);
            } else {
               value->publish_failures = 0;
               value->next_republish = std::chrono::steady_clock::now() + republish_delay(*value);
            }
         }
      }
   } catch (...) {
      failure = std::current_exception();
   }
   try {
      co_await callbacks_.remove(value->registration.protocol, value->registration.key);
   } catch (...) {
      failure = std::current_exception();
   }
   finish_entry(value, failure);
}

void dht_provider_registry::notify_endpoints_changed() noexcept {
   try {
      {
         const auto lock = std::scoped_lock{mutex_};
         if (endpoint_generation_ != (std::numeric_limits<std::uint64_t>::max)()) {
            ++endpoint_generation_;
         }
      }
      changed_->notify();
   } catch (...) {
   }
}

void dht_provider_registry::open_admission() noexcept {
   try {
      {
         const auto lock = std::scoped_lock{mutex_};
         if (sealed_ || admission_open_) {
            return;
         }
         admission_open_ = true;
      }
      changed_->notify();
   } catch (...) {
   }
}

void dht_provider_registry::seal() noexcept {
   try {
      {
         const auto lock = std::scoped_lock{mutex_};
         if (sealed_) {
            return;
         }
         sealed_ = true;
         for (auto& [_, value] : entries_) {
            value->stop_requested = true;
         }
      }
      admission_->close();
      changed_->notify();
   } catch (...) {
   }
}

boost::asio::awaitable<void> dht_provider_registry::async_drain() {
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   auto observed = changed_->epoch();
   while (true) {
      auto failure = std::exception_ptr{};
      auto retry = std::shared_ptr<entry>{};
      {
         const auto lock = std::scoped_lock{mutex_};
         if (drain_failure_) {
            failure = std::exchange(drain_failure_, {});
         } else if (sealed_ && admissions_in_flight_ == 0) {
            const auto found = std::ranges::find_if(entries_, [](const auto& item) {
               return item.second->removal_failed && !item.second->removal_in_flight;
            });
            if (found != entries_.end()) {
               retry = found->second;
               retry->removal_in_flight = true;
               retry->removal_failed = false;
               retry->removal_failure = {};
            } else if (entries_.empty()) {
               co_return;
            }
         }
      }
      if (failure) {
         std::rethrow_exception(failure);
      }
      if (retry) {
         reset_owners_for_retry(retry);
         co_await async_remove(retry);
         continue;
      }
      observed = co_await changed_->async_wait(observed);
   }
}

} // namespace forge::net::p2p::detail
