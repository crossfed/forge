module;

#include <chrono>
#include <cstdint>
#include <memory>

#include <boost/asio/awaitable.hpp>

export module forge.asio.notification;

export namespace forge::asio {

class notification {
 public:
   using epoch_type = std::uint64_t;

   notification();
   ~notification();

   notification(notification&&) noexcept;
   notification& operator=(notification&&) noexcept;

   notification(const notification&) = delete;
   notification& operator=(const notification&) = delete;

   [[nodiscard]] epoch_type epoch() const noexcept;
   boost::asio::awaitable<epoch_type> async_wait(epoch_type observed_epoch);
   boost::asio::awaitable<epoch_type> async_wait_until(
       epoch_type observed_epoch, std::chrono::steady_clock::time_point deadline);
   void notify() noexcept;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::asio
