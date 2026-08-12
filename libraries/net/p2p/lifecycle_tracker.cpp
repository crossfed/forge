module;

#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <map>
#include <mutex>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.net.p2p.lifecycle;

#include "details/lifecycle_tracker.hxx"

namespace forge::net::p2p::detail {
lifecycle_tracker::waiter::waiter(boost::asio::any_io_executor executor)
    : strand{boost::asio::make_strand(std::move(executor))},
      timer{strand, boost::asio::steady_timer::time_point::max()} {}

lifecycle_tracker::state::cancellation_context::cancellation_context(boost::asio::any_io_executor executor)
    : strand{boost::asio::make_strand(std::move(executor))} {}

void lifecycle_tracker::wake(const std::shared_ptr<waiter>& waiter) noexcept {
   try {
      boost::asio::dispatch(waiter->strand, [waiter] {
         try {
            waiter->timer.expires_at(boost::asio::steady_timer::time_point::min());
            waiter->timer.cancel();
         } catch (...) {
            // Lifecycle completion notification is best effort and noexcept.
         }
      });
   } catch (...) {
      try {
         waiter->timer.expires_at(boost::asio::steady_timer::time_point::min());
         waiter->timer.cancel();
      } catch (...) {
         // Lifecycle completion notification is best effort and noexcept.
      }
   }
}

lifecycle_tracker::state::state(boost::asio::any_io_executor executor_value) : executor{std::move(executor_value)} {}

void lifecycle_tracker::state::release(std::uint64_t id) noexcept {
   auto ready = std::vector<std::shared_ptr<waiter>>{};
   try {
      {
         const auto lock = std::scoped_lock{mutex};
         operations.erase(id);
         if (stop_requested && operations.empty()) {
            ready.swap(waiters);
         }
      }
      for (const auto& item : ready) {
         wake(item);
      }
   } catch (...) {
      // Operation destruction must remain noexcept during process teardown.
   }
}

lifecycle_tracker::operation::operation(std::shared_ptr<state> state_value, std::uint64_t id,
                                        std::shared_ptr<state::cancellation_context> cancellation)
    : state_{std::move(state_value)}, id_{id}, cancellation_{std::move(cancellation)} {}

lifecycle_tracker::operation::operation(operation&& other) noexcept
    : state_{std::move(other.state_)}, id_{std::exchange(other.id_, 0)}, cancellation_{std::move(other.cancellation_)} {
}

lifecycle_tracker::operation& lifecycle_tracker::operation::operator=(operation&& other) noexcept {
   if (this != &other) {
      release();
      state_ = std::move(other.state_);
      id_ = std::exchange(other.id_, 0);
      cancellation_ = std::move(other.cancellation_);
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
   return cancellation_ ? boost::asio::any_io_executor{cancellation_->strand} : boost::asio::any_io_executor{};
}

boost::asio::cancellation_slot lifecycle_tracker::operation::cancellation_slot() const noexcept {
   return cancellation_ ? cancellation_->signal.slot() : boost::asio::cancellation_slot{};
}

std::shared_ptr<const std::atomic_bool> lifecycle_tracker::operation::stop_latch() const noexcept {
   return cancellation_ ? cancellation_->stop_requested : nullptr;
}

void lifecycle_tracker::operation::release() noexcept {
   cancellation_.reset();
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

lifecycle_tracker::operation lifecycle_tracker::track() noexcept {
   try {
      const auto cancellation = std::make_shared<state::cancellation_context>(state_->executor);
      const auto lock = std::scoped_lock{state_->mutex};
      if (state_->stop_requested) {
         return {};
      }
      const auto id = state_->next_operation_id++;
      state_->operations.emplace(id, cancellation);
      return operation{state_, id, cancellation};
   } catch (...) {
      return {};
   }
}

void lifecycle_tracker::request_stop() noexcept {
   auto ready = std::vector<std::shared_ptr<waiter>>{};
   try {
      {
         const auto lock = std::scoped_lock{state_->mutex};
         if (state_->stop_requested) {
            return;
         }
         state_->stop_requested = true;
         state_->phase = lifecycle_phase::stopping;
         for (const auto& [_, cancellation] : state_->operations) {
            cancellation->stop_requested->store(true, std::memory_order_release);
            try {
               boost::asio::post(cancellation->strand,
                                 [cancellation] { cancellation->signal.emit(boost::asio::cancellation_type::all); });
            } catch (...) {
               // Resource teardown remains the final cancellation fallback.
            }
         }
         if (state_->operations.empty()) {
            ready.swap(state_->waiters);
         }
      }
      for (const auto& waiter : ready) {
         wake(waiter);
      }
   } catch (...) {
      // Node-level resource teardown remains the final shutdown fallback.
   }
}

boost::asio::awaitable<void> lifecycle_tracker::wait() const {
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   const auto executor = co_await boost::asio::this_coro::executor;
   auto waiter = std::make_shared<lifecycle_tracker::waiter>(executor);

   auto switch_error = boost::system::error_code{};
   co_await boost::asio::dispatch(waiter->strand,
                                  boost::asio::redirect_error(boost::asio::use_awaitable, switch_error));
   if (switch_error) {
      throw boost::system::system_error{switch_error};
   }

   auto ready = false;
   {
      const auto lock = std::scoped_lock{state_->mutex};
      ready = state_->stop_requested && state_->operations.empty();
      if (!ready) {
         state_->waiters.push_back(waiter);
      }
   }
   if (!ready) {
      auto error = boost::system::error_code{};
      co_await waiter->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      static_cast<void>(error);
   }
}

void lifecycle_tracker::finish_stop() noexcept {
   auto ready = std::vector<std::shared_ptr<waiter>>{};
   try {
      {
         const auto lock = std::scoped_lock{state_->mutex};
         state_->stop_requested = true;
         state_->phase = lifecycle_phase::stopped;
         if (state_->operations.empty()) {
            ready.swap(state_->waiters);
         }
      }
      for (const auto& waiter : ready) {
         wake(waiter);
      }
   } catch (...) {
      // Final teardown must remain noexcept.
   }
}

} // namespace forge::net::p2p::detail
