#pragma once

#include <chrono>
#include <cstdint>

#include <boost/asio/awaitable.hpp>

namespace forge::net::p2p::detail {

class lifecycle_wakeup {
 public:
   using epoch_type = std::uint64_t;

   [[nodiscard]] epoch_type epoch() const noexcept;
   boost::asio::awaitable<epoch_type> async_wait(epoch_type observed);
   boost::asio::awaitable<epoch_type> async_wait_until(epoch_type observed,
                                                       std::chrono::steady_clock::time_point deadline);
   void notify() noexcept;

 private:
   forge::asio::notification notification_;
};

} // namespace forge::net::p2p::detail
