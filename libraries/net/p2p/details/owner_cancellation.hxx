#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_signal.hpp>

#include <functional>
#include <memory>

#include "worker_stop_bridge.hxx"

namespace forge::net::p2p::detail {

class owner_stream_cancellation final {
 public:
   owner_stream_cancellation(boost::asio::cancellation_slot slot,
                             std::shared_ptr<forge::net::p2p::stream> stream);
   ~owner_stream_cancellation() noexcept;

   owner_stream_cancellation(const owner_stream_cancellation&) = delete;
   owner_stream_cancellation& operator=(const owner_stream_cancellation&) = delete;
   owner_stream_cancellation(owner_stream_cancellation&&) = delete;
   owner_stream_cancellation& operator=(owner_stream_cancellation&&) = delete;

   void request_cancel() noexcept;

 private:
   boost::asio::cancellation_slot slot_;
   std::shared_ptr<forge::net::p2p::stream> stream_;
};

using owner_cancellable_work =
    std::function<boost::asio::awaitable<void>(boost::asio::cancellation_slot)>;

// Runs one operation and its stop waiter on a single strand. co_spawn installs
// the child's cancellation slot before the child publishes its terminal
// callback, and the parent structurally joins the child before returning.
boost::asio::awaitable<void>
async_run_with_owner_cancellation(std::shared_ptr<worker_stop_bridge> stop,
                                  owner_cancellable_work work,
                                  worker_stop_bridge_options options = {});

} // namespace forge::net::p2p::detail
