module;

#include <cstddef>
#include <mutex>
#include <utility>

#include <boost/asio/awaitable.hpp>

module forge.net.yamux.session;

import forge.asio.notification;

#include "details/transport_write_tracker.hxx"

namespace forge::net::yamux::detail {

transport_write_tracker::reservation::reservation(transport_write_tracker& owner) noexcept : owner_{&owner} {}

transport_write_tracker::reservation::~reservation() {
   release();
}

transport_write_tracker::reservation::reservation(reservation&& other) noexcept
    : owner_{std::exchange(other.owner_, nullptr)} {}

transport_write_tracker::reservation&
transport_write_tracker::reservation::operator=(reservation&& other) noexcept {
   if (this != &other) {
      release();
      owner_ = std::exchange(other.owner_, nullptr);
   }
   return *this;
}

void transport_write_tracker::reservation::release() noexcept {
   if (owner_) {
      std::exchange(owner_, nullptr)->release();
   }
}

transport_write_tracker::reservation transport_write_tracker::reserve() {
   auto lock = std::scoped_lock{mutex_};
   ++active_;
   return reservation{*this};
}

boost::asio::awaitable<void> transport_write_tracker::async_wait() {
   while (true) {
      const auto observed = changed_.epoch();
      {
         auto lock = std::scoped_lock{mutex_};
         if (active_ == 0) {
            co_return;
         }
      }
      (void)co_await changed_.async_wait(observed);
   }
}

void transport_write_tracker::release() noexcept {
   {
      auto lock = std::scoped_lock{mutex_};
      if (active_ > 0) {
         --active_;
      }
   }
   changed_.notify();
}

} // namespace forge::net::yamux::detail
