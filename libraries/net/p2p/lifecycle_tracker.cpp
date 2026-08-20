module;

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>

module forge.net.p2p.node;

import forge.asio.notification;
import forge.net.p2p.lifecycle;

#include "details/lifecycle_tracker.hxx"
#include "details/lifecycle_wakeup.hxx"

namespace forge::net::p2p::detail {
lifecycle_tracker::state::operation_context::operation_context(boost::asio::any_io_executor executor)
    : strand{boost::asio::make_strand(std::move(executor))} {}

lifecycle_tracker::state::state(boost::asio::any_io_executor executor_value)
    : executor{std::move(executor_value)}, changed{std::make_shared<lifecycle_wakeup>()} {}

void lifecycle_tracker::state::release(std::uint64_t id) noexcept {
   try {
      auto notify = false;
      {
         const auto lock = std::scoped_lock{mutex};
         operations.erase(id);
         notify = stop_requested && operations.empty();
      }
      if (notify) {
         changed->notify();
      }
   } catch (...) {
      // Operation destruction must remain noexcept during process teardown.
   }
}

lifecycle_tracker::operation::operation(std::shared_ptr<state> state_value, std::uint64_t id,
                                        std::shared_ptr<state::operation_context> context)
    : state_{std::move(state_value)}, id_{id}, context_{std::move(context)} {}

lifecycle_tracker::operation::operation(operation&& other) noexcept
    : state_{std::move(other.state_)}, id_{std::exchange(other.id_, 0)}, context_{std::move(other.context_)} {}

lifecycle_tracker::operation& lifecycle_tracker::operation::operator=(operation&& other) noexcept {
   if (this != &other) {
      release();
      state_ = std::move(other.state_);
      id_ = std::exchange(other.id_, 0);
      context_ = std::move(other.context_);
   }
   return *this;
}

lifecycle_tracker::operation::~operation() {
   release();
}

bool lifecycle_tracker::operation::active() const noexcept {
   return state_ != nullptr;
}

boost::asio::any_io_executor lifecycle_tracker::operation::executor() const noexcept {
   return context_ ? boost::asio::any_io_executor{context_->strand} : boost::asio::any_io_executor{};
}

std::shared_ptr<const std::atomic_bool> lifecycle_tracker::operation::stop_latch() const noexcept {
   return state_ ? state_->stop_latch : nullptr;
}

void lifecycle_tracker::operation::release() noexcept {
   context_.reset();
   if (auto state = std::move(state_)) {
      state->release(std::exchange(id_, 0));
   }
}

lifecycle_tracker::lifecycle_tracker(boost::asio::any_io_executor executor)
    : state_{std::make_shared<state>(std::move(executor))} {}

bool lifecycle_tracker::begin_start() noexcept {
   const auto lock = std::scoped_lock{state_->mutex};
   if (state_->stop_requested || state_->phase != lifecycle_phase::idle) {
      return false;
   }
   state_->phase = lifecycle_phase::hydrating;
   return true;
}

void lifecycle_tracker::set_phase(lifecycle_phase value) noexcept {
   const auto lock = std::scoped_lock{state_->mutex};
   if (!state_->stop_requested) {
      state_->phase = value;
   }
}

lifecycle_phase lifecycle_tracker::phase() const noexcept {
   const auto lock = std::scoped_lock{state_->mutex};
   return state_->phase;
}

bool lifecycle_tracker::stop_requested() const noexcept {
   return state_->stop_latch->load(std::memory_order_acquire);
}

lifecycle_tracker::operation lifecycle_tracker::track() noexcept {
   try {
      const auto context = std::make_shared<state::operation_context>(state_->executor);
      const auto lock = std::scoped_lock{state_->mutex};
      if (state_->stop_requested) {
         return {};
      }
      const auto id = state_->next_operation_id++;
      state_->operations.emplace(id, context);
      return operation{state_, id, context};
   } catch (...) {
      return {};
   }
}

void lifecycle_tracker::request_stop() noexcept {
   try {
      {
         const auto lock = std::scoped_lock{state_->mutex};
         if (state_->stop_requested) {
            return;
         }
         state_->stop_requested = true;
         state_->phase = lifecycle_phase::stopping;
         state_->stop_latch->store(true, std::memory_order_release);
      }
      state_->changed->notify();
   } catch (...) {
      // Node-level resource teardown remains the final shutdown fallback.
   }
}

boost::asio::awaitable<void> lifecycle_tracker::wait() const {
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   auto observed = state_->changed->epoch();
   while (true) {
      {
         const auto lock = std::scoped_lock{state_->mutex};
         if (state_->stop_requested && state_->operations.empty()) {
            co_return;
         }
      }
      observed = co_await state_->changed->async_wait(observed);
   }
}

void lifecycle_tracker::finish_stop() noexcept {
   try {
      {
         const auto lock = std::scoped_lock{state_->mutex};
         state_->stop_requested = true;
         state_->phase = lifecycle_phase::stopped;
      }
      state_->changed->notify();
   } catch (...) {
      // Final teardown must remain noexcept.
   }
}

} // namespace forge::net::p2p::detail
