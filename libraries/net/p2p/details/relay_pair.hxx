#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

#include "relay_budget.hxx"

namespace forge::net::p2p::detail {

class relay_pair {
 public:
   relay_pair(peer_id owner_value, forge::net::p2p::stream left_value, forge::net::p2p::stream right_value,
              resource_manager::stream_reservation resource_value, boost::asio::any_io_executor executor,
              std::chrono::milliseconds duration, std::uint64_t byte_limit);

   [[nodiscard]] bool mark_finished() noexcept;
   boost::asio::awaitable<bool> async_wait_deadline();
   void cancel_streams() noexcept;

   peer_id owner;
   forge::net::p2p::stream left;
   forge::net::p2p::stream right;
   resource_manager::stream_reservation resource;
   relay_budget left_to_right;
   relay_budget right_to_left;

 private:
   void cancel_deadline() noexcept;

   std::shared_ptr<boost::asio::steady_timer> deadline_;
   std::mutex mutex_;
   std::uint32_t finished_ = 0;
   bool deadline_cancelled_ = false;
};

} // namespace forge::net::p2p::detail
