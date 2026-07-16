#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

#include <memory>

namespace forge::asio::detail {

class async_waiter : public std::enable_shared_from_this<async_waiter> {
 public:
   explicit async_waiter(boost::asio::any_io_executor executor);

   boost::asio::awaitable<boost::system::error_code> wait();
   void wake() noexcept;

 private:
   boost::asio::strand<boost::asio::any_io_executor> strand_;
   boost::asio::steady_timer timer_;
};

} // namespace forge::asio::detail
