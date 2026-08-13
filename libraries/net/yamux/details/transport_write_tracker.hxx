#pragma once

namespace forge::net::yamux::detail {

class transport_write_tracker {
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
      explicit reservation(transport_write_tracker& owner) noexcept;

      transport_write_tracker* owner_ = nullptr;

      friend class transport_write_tracker;
   };

   [[nodiscard]] reservation reserve();
   boost::asio::awaitable<void> async_wait();

 private:
   void release() noexcept;

   std::mutex mutex_;
   std::size_t active_ = 0;
   forge::asio::notification changed_;
};

} // namespace forge::net::yamux::detail
