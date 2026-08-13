module;

#include <cstddef>
#include <chrono>
#include <mutex>
#include <optional>
#include <utility>

#include <boost/asio/awaitable.hpp>

module forge.net.yamux.session;

import forge.asio.notification;

#include "details/transport_write_tracker.hxx"

namespace forge::net::yamux::detail {

transport_write_tracker::reservation::reservation(std::shared_ptr<state> owner) noexcept : owner_{std::move(owner)} {}

transport_write_tracker::reservation::~reservation() {
   release();
}

transport_write_tracker::reservation::reservation(reservation&& other) noexcept : owner_{std::move(other.owner_)} {}

transport_write_tracker::reservation& transport_write_tracker::reservation::operator=(reservation&& other) noexcept {
   if (this != &other) {
      release();
      owner_ = std::move(other.owner_);
   }
   return *this;
}

void transport_write_tracker::reservation::release() noexcept {
   auto owner = std::move(owner_);
   if (!owner) {
      return;
   }
   {
      auto lock = std::scoped_lock{owner->mutex};
      if (owner->active > 0) {
         --owner->active;
      }
   }
   owner->changed.notify();
}

std::optional<transport_write_tracker::reservation> transport_write_tracker::try_reserve() {
   auto owner = state_;
   auto lock = std::scoped_lock{owner->mutex};
   if (owner->sealed) {
      return std::nullopt;
   }
   ++owner->active;
   return reservation{std::move(owner)};
}

void transport_write_tracker::seal() noexcept {
   auto owner = state_;
   {
      auto lock = std::scoped_lock{owner->mutex};
      owner->sealed = true;
   }
   owner->changed.notify();
}

boost::asio::awaitable<void> transport_write_tracker::async_wait() {
   auto owner = state_;
   while (true) {
      const auto observed = owner->changed.epoch();
      {
         auto lock = std::scoped_lock{owner->mutex};
         if (owner->active == 0) {
            co_return;
         }
      }
      (void)co_await owner->changed.async_wait(observed);
   }
}

boost::asio::awaitable<bool> transport_write_tracker::async_wait_until(std::chrono::steady_clock::time_point deadline) {
   auto owner = state_;
   while (true) {
      const auto observed = owner->changed.epoch();
      {
         auto lock = std::scoped_lock{owner->mutex};
         if (owner->active == 0) {
            co_return true;
         }
      }
      (void)co_await owner->changed.async_wait_until(observed, deadline);
      {
         auto lock = std::scoped_lock{owner->mutex};
         if (owner->active == 0) {
            co_return true;
         }
      }
      if (std::chrono::steady_clock::now() >= deadline) {
         co_return false;
      }
   }
}

} // namespace forge::net::yamux::detail
