#pragma once

namespace forge::net::yamux::detail {

class transport_write_tracker {
 private:
   struct state {
      std::mutex mutex;
      std::size_t active = 0;
      bool sealed = false;
      forge::asio::notification changed;
   };

 public:
   class reservation {
    public:
      reservation() = default;
      ~reservation();

      reservation(const reservation&) = delete;
      reservation& operator=(const reservation&) = delete;
      reservation(reservation&& other) noexcept;
      reservation& operator=(reservation&& other) noexcept;

      void release() noexcept;

    private:
      explicit reservation(std::shared_ptr<state> owner) noexcept;

      std::shared_ptr<state> owner_;

      friend class transport_write_tracker;
   };

   [[nodiscard]] std::optional<reservation> try_reserve();
   void seal() noexcept;
   boost::asio::awaitable<void> async_wait();
   boost::asio::awaitable<bool> async_wait_until(std::chrono::steady_clock::time_point deadline);

 private:
   std::shared_ptr<state> state_ = std::make_shared<state>();
};

} // namespace forge::net::yamux::detail
