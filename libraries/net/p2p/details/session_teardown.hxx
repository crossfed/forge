#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace forge::net::p2p::detail {

class session_teardown {
 public:
   struct operation {
      std::function<boost::asio::awaitable<void>()> close;
      std::function<void()> cancel;
   };

   explicit session_teardown(boost::asio::any_io_executor executor);

   void start(std::vector<operation> operations) noexcept;
   boost::asio::awaitable<void> wait() const;

 private:
   struct state;
   std::shared_ptr<state> state_;
};

} // namespace forge::net::p2p::detail
