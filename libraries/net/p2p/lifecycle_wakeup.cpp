module;

#include <chrono>
#include <cstdint>

#include <boost/asio/awaitable.hpp>

module forge.net.p2p.node;

import forge.asio.notification;

#include "details/lifecycle_wakeup.hxx"

namespace forge::net::p2p::detail {

lifecycle_wakeup::epoch_type lifecycle_wakeup::epoch() const noexcept {
   return notification_.epoch();
}

boost::asio::awaitable<lifecycle_wakeup::epoch_type> lifecycle_wakeup::async_wait(epoch_type observed) {
   co_return co_await notification_.async_wait(observed);
}

boost::asio::awaitable<lifecycle_wakeup::epoch_type>
lifecycle_wakeup::async_wait_until(epoch_type observed, std::chrono::steady_clock::time_point deadline) {
   co_return co_await notification_.async_wait_until(observed, deadline);
}

void lifecycle_wakeup::notify() noexcept {
   notification_.notify();
}

} // namespace forge::net::p2p::detail
