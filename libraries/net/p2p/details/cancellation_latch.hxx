#pragma once

namespace forge::net::p2p {

class cancellation_latch {
 public:
   void arm(std::function<void()> cancel);
   void request_stop() noexcept;
   void clear() noexcept;
   [[nodiscard]] bool finish() noexcept;

 private:
   enum class state { open, stop_requested, completed, stopped };

   void complete_callback() noexcept;

   std::mutex mutex_;
   std::condition_variable completion_;
   std::function<void()> cancel_;
   state state_ = state::open;
   unsigned active_callbacks_ = 0;
};

} // namespace forge::net::p2p
