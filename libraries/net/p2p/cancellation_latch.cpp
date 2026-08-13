module;

#include <condition_variable>
#include <functional>
#include <mutex>
#include <utility>

module forge.net.p2p.node;

#include "details/cancellation_latch.hxx"

namespace forge::net::p2p {

void cancellation_latch::arm(std::function<void()> cancel) {
   auto invoke = std::function<void()>{};
   {
      auto lock = std::scoped_lock{mutex_};
      if (state_ == state::completed || state_ == state::stopped) {
         return;
      }
      if (state_ == state::open) {
         cancel_ = std::move(cancel);
         return;
      }
      ++active_callbacks_;
      invoke = std::move(cancel);
   }
   try {
      if (invoke) {
         invoke();
      }
   } catch (...) {
   }
   complete_callback();
}

void cancellation_latch::request_stop() noexcept {
   auto cancel = std::function<void()>{};
   {
      auto lock = std::scoped_lock{mutex_};
      if (state_ != state::open) {
         return;
      }
      state_ = state::stop_requested;
      cancel = std::move(cancel_);
      active_callbacks_ += static_cast<unsigned>(static_cast<bool>(cancel));
   }
   try {
      if (cancel) {
         cancel();
      }
   } catch (...) {
   }
   if (cancel) {
      complete_callback();
   }
}

void cancellation_latch::clear() noexcept {
   auto lock = std::unique_lock{mutex_};
   completion_.wait(lock, [this] { return active_callbacks_ == 0; });
   cancel_ = {};
}

[[nodiscard]] bool cancellation_latch::finish() noexcept {
   auto lock = std::unique_lock{mutex_};
   if (state_ == state::open) {
      state_ = state::completed;
   } else if (state_ == state::stop_requested) {
      state_ = state::stopped;
   }
   cancel_ = {};
   completion_.wait(lock, [this] { return active_callbacks_ == 0; });
   return state_ == state::completed;
}

void cancellation_latch::complete_callback() noexcept {
   {
      auto lock = std::scoped_lock{mutex_};
      if (active_callbacks_ != 0) {
         --active_callbacks_;
      }
   }
   completion_.notify_all();
}

} // namespace forge::net::p2p
