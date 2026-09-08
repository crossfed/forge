module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/system/system_error.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

module forge.plugins.chain.signer.plugin;

import forge.api.core.exceptions;
import forge.asio.notification;
import forge.chain.api.exceptions;

#include "details/admission.hxx"

namespace forge::plugins::chain::signer {
namespace {

enum class waiter_state : std::uint8_t {
   queued,
   granted,
   canceled,
   stopped,
};

struct waiter {
   std::string caller;
   std::size_t bytes = 0;
   waiter_state state = waiter_state::queued;
   std::shared_ptr<admission_operation> active;
};

} // namespace

struct admission_operation {
   void cancel(boost::asio::cancellation_type type) noexcept {
      if (canceled.exchange(true, std::memory_order_acq_rel)) {
         return;
      }
      cancellation.emit(type);
   }

   [[nodiscard]] bool cancelled() const noexcept {
      return canceled.load(std::memory_order_acquire);
   }

   std::atomic_bool canceled = false;
   boost::asio::cancellation_signal cancellation;
};

struct admission_state {
   admission_state(std::size_t inflight_limit, std::size_t queued_limit, std::size_t queued_byte_limit,
                   std::size_t caller_limit)
       : max_inflight{inflight_limit}, max_queued{queued_limit}, max_queued_bytes{queued_byte_limit},
         max_per_caller{caller_limit} {}

   [[nodiscard]] bool caller_full_locked(const std::string& caller) const {
      const auto found = caller_counts.find(caller);
      return found != caller_counts.end() && found->second >= max_per_caller;
   }

   void reserve_caller_locked(const std::string& caller) {
      ++caller_counts[caller];
   }

   void release_caller_locked(const std::string& caller) noexcept {
      const auto found = caller_counts.find(caller);
      if (found == caller_counts.end()) {
         return;
      }
      if (--found->second == 0U) {
         caller_counts.erase(found);
      }
   }

   [[nodiscard]] std::shared_ptr<admission_operation> make_operation_locked() {
      auto result = std::make_shared<admission_operation>();
      operations.push_back(result);
      return result;
   }

   void grant_locked() noexcept {
      while (!stopping && active < max_inflight && !waiters.empty()) {
         auto next = std::move(waiters.front());
         waiters.pop_front();
         if (next->state != waiter_state::queued) {
            continue;
         }
         queued_bytes -= next->bytes;
         next->state = waiter_state::granted;
         ++active;
      }
   }

   void release(const std::string& caller) noexcept {
      {
         const auto lock = std::scoped_lock{mutex};
         if (active != 0U) {
            --active;
         }
         release_caller_locked(caller);
         std::erase_if(operations, [](const auto& value) { return value.expired(); });
         grant_locked();
      }
      changed.notify();
   }

   mutable std::mutex mutex;
   std::size_t max_inflight = 0;
   std::size_t max_queued = 0;
   std::size_t max_queued_bytes = 0;
   std::size_t max_per_caller = 0;
   std::size_t active = 0;
   std::size_t queued_bytes = 0;
   bool stopping = false;
   std::deque<std::shared_ptr<waiter>> waiters;
   std::unordered_map<std::string, std::size_t> caller_counts;
   std::vector<std::weak_ptr<admission_operation>> operations;
   forge::asio::notification changed;
};

admission_lease::admission_lease(std::shared_ptr<admission_state> owner, std::shared_ptr<admission_operation> active,
                                 std::string caller)
    : owner_{std::move(owner)}, active_{std::move(active)}, caller_{std::move(caller)} {}

admission_lease::~admission_lease() {
   release();
}

admission_lease::admission_lease(admission_lease&& other) noexcept
    : owner_{std::move(other.owner_)}, active_{std::move(other.active_)}, caller_{std::move(other.caller_)} {}

admission_lease& admission_lease::operator=(admission_lease&& other) noexcept {
   if (this != &other) {
      release();
      owner_ = std::move(other.owner_);
      active_ = std::move(other.active_);
      caller_ = std::move(other.caller_);
   }
   return *this;
}

admission_cancellation admission_lease::bind(boost::asio::cancellation_state inherited) {
   auto parent = inherited.slot();
   auto active = active_;
   if (parent.is_connected()) {
      parent.assign([active](boost::asio::cancellation_type type) {
         if (type != boost::asio::cancellation_type::none) {
            active->cancel(type);
         }
      });
   }
   if (inherited.cancelled() != boost::asio::cancellation_type::none) {
      active->cancel(inherited.cancelled());
   }
   return admission_cancellation{std::move(parent), std::move(active)};
}

boost::asio::cancellation_slot admission_lease::cancellation_slot() noexcept {
   return active_ == nullptr ? boost::asio::cancellation_slot{} : active_->cancellation.slot();
}

bool admission_lease::cancelled() const noexcept {
   return active_ != nullptr && active_->cancelled();
}

void admission_lease::cancel(boost::asio::cancellation_type type) noexcept {
   if (active_ != nullptr && type != boost::asio::cancellation_type::none) {
      active_->cancel(type);
   }
}

void admission_lease::release() noexcept {
   auto owner = std::move(owner_);
   active_.reset();
   if (owner != nullptr) {
      owner->release(caller_);
      caller_.clear();
   }
}

admission_cancellation::admission_cancellation(boost::asio::cancellation_slot parent,
                                               std::shared_ptr<admission_operation> active)
    : parent_{std::move(parent)}, active_{std::move(active)} {}

admission_cancellation::~admission_cancellation() {
   parent_.clear();
}

admission_cancellation::admission_cancellation(admission_cancellation&& other) noexcept
    : parent_{std::move(other.parent_)}, active_{std::move(other.active_)} {}

admission_cancellation& admission_cancellation::operator=(admission_cancellation&& other) noexcept {
   if (this != &other) {
      parent_.clear();
      parent_ = std::move(other.parent_);
      active_ = std::move(other.active_);
   }
   return *this;
}

boost::asio::cancellation_slot admission_cancellation::slot() noexcept {
   return active_->cancellation.slot();
}

admission_queue::admission_queue(std::size_t max_inflight, std::size_t max_queued, std::size_t max_queued_bytes,
                                 std::size_t max_per_caller)
    : state_{std::make_shared<admission_state>(max_inflight, max_queued, max_queued_bytes, max_per_caller)} {}

boost::asio::awaitable<admission_lease> admission_queue::acquire(std::string caller, std::size_t bytes) {
   auto cancellation = co_await boost::asio::this_coro::cancellation_state;
   if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled, "chain signer admission was canceled");
   }

   auto state = state_;
   auto pending = std::shared_ptr<waiter>{};
   auto observed = state->changed.epoch();
   {
      const auto lock = std::scoped_lock{state->mutex};
      if (state->stopping) {
         FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::unavailable, "chain signer admission is closed");
      }
      if (state->caller_full_locked(caller)) {
         FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::resource_exhausted,
                               "chain signer caller quota is exhausted");
      }
      if (state->waiters.empty() && state->active < state->max_inflight) {
         state->reserve_caller_locked(caller);
         ++state->active;
         auto active = state->make_operation_locked();
         co_return admission_lease{std::move(state), std::move(active), std::move(caller)};
      }
      if (state->waiters.size() >= state->max_queued || bytes > state->max_queued_bytes - state->queued_bytes) {
         FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::resource_exhausted,
                               "chain signer admission queue budget is exhausted");
      }
      state->reserve_caller_locked(caller);
      state->queued_bytes += bytes;
      pending = std::make_shared<waiter>(waiter{
          .caller = std::move(caller),
          .bytes = bytes,
          .active = state->make_operation_locked(),
      });
      state->waiters.push_back(pending);
   }

   while (true) {
      try {
         observed = co_await state->changed.async_wait(observed);
      } catch (const boost::system::system_error&) {
         auto release_grant = false;
         {
            const auto lock = std::scoped_lock{state->mutex};
            if (pending->state == waiter_state::queued) {
               pending->state = waiter_state::canceled;
               state->queued_bytes -= pending->bytes;
               state->release_caller_locked(pending->caller);
               std::erase(state->waiters, pending);
            } else if (pending->state == waiter_state::granted) {
               pending->state = waiter_state::canceled;
               release_grant = true;
            }
         }
         if (release_grant) {
            pending->active.reset();
            state->release(pending->caller);
         }
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled, "chain signer admission was canceled");
      }

      cancellation = co_await boost::asio::this_coro::cancellation_state;
      auto granted = false;
      auto stopped = false;
      auto notify = false;
      {
         const auto lock = std::scoped_lock{state->mutex};
         granted = pending->state == waiter_state::granted;
         stopped = pending->state == waiter_state::stopped;
         if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
            if (pending->state == waiter_state::queued) {
               pending->state = waiter_state::canceled;
               state->queued_bytes -= pending->bytes;
               state->release_caller_locked(pending->caller);
               std::erase(state->waiters, pending);
            } else if (granted) {
               pending->state = waiter_state::canceled;
               --state->active;
               state->release_caller_locked(pending->caller);
               pending->active.reset();
               state->grant_locked();
               notify = true;
            }
            granted = false;
         }
      }

      if (notify) {
         state->changed.notify();
      }

      if (granted) {
         co_return admission_lease{std::move(state), std::move(pending->active), std::move(pending->caller)};
      }
      if (stopped) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled,
                               "chain signer queued request was canceled during shutdown");
      }
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled, "chain signer admission was canceled");
      }
   }
}

void admission_queue::close() noexcept {
   auto pending = std::deque<std::shared_ptr<waiter>>{};
   auto active = std::vector<std::shared_ptr<admission_operation>>{};
   {
      const auto lock = std::scoped_lock{state_->mutex};
      if (state_->stopping) {
         return;
      }
      state_->stopping = true;
      pending.swap(state_->waiters);
      state_->queued_bytes = 0;
      for (const auto& value : pending) {
         if (value->state == waiter_state::queued) {
            value->state = waiter_state::stopped;
            state_->release_caller_locked(value->caller);
         }
      }
      for (const auto& operation : state_->operations) {
         if (auto value = operation.lock()) {
            active.push_back(std::move(value));
         }
      }
   }
   for (const auto& operation : active) {
      operation->cancel(boost::asio::cancellation_type::all);
   }
   state_->changed.notify();
}

boost::asio::awaitable<void> admission_queue::wait_for_drain() {
   auto state = state_;
   while (true) {
      const auto observed = state->changed.epoch();
      {
         const auto lock = std::scoped_lock{state->mutex};
         if (state->active == 0U) {
            co_return;
         }
      }
      static_cast<void>(co_await state->changed.async_wait(observed));
   }
}

bool admission_queue::closed() const noexcept {
   const auto lock = std::scoped_lock{state_->mutex};
   return state_->stopping;
}

} // namespace forge::plugins::chain::signer
