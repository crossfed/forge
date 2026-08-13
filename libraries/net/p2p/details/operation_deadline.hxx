#pragma once

namespace forge::net::p2p {

void validate_operation_timeout(std::chrono::milliseconds timeout, std::string_view name);
[[noreturn]] void throw_operation_timeout(std::string_view operation);

class operation_deadline {
 private:
   enum class state_value : std::uint8_t { pending, completed, timed_out, stopped };

   struct shared_state {
      std::mutex mutex;
      std::function<void()> cancel;
      state_value value = state_value::pending;
      bool finished = false;
      bool cancel_invoked = false;
      std::atomic_size_t active_callbacks{0};
   };

   struct callback_claim {
      std::shared_ptr<shared_state> state;
      std::function<void()> callback;
   };

 public:
   class stop_token {
    public:
      stop_token() = default;

      [[nodiscard]] bool request_stop() const noexcept;

    private:
      friend class operation_deadline;
      explicit stop_token(std::shared_ptr<shared_state> state);

      std::shared_ptr<shared_state> state_;
   };

   operation_deadline(boost::asio::io_context& context, std::chrono::milliseconds timeout);
   operation_deadline(const operation_deadline&) = delete;
   operation_deadline& operator=(const operation_deadline&) = delete;
   ~operation_deadline();

   void arm(std::function<void()> cancel);
   [[nodiscard]] bool finish() noexcept;
   void cancel() noexcept;
   [[nodiscard]] stop_token stopping() const noexcept;
   [[nodiscard]] bool timed_out() const noexcept;
   [[nodiscard]] bool stopped() const noexcept;

 private:
   [[nodiscard]] static callback_claim claim_cancel_locked(const std::shared_ptr<shared_state>& state);
   static void invoke_cancel(callback_claim claim) noexcept;

   std::shared_ptr<boost::asio::steady_timer> timer_;
   std::shared_ptr<shared_state> state_;
};

} // namespace forge::net::p2p
