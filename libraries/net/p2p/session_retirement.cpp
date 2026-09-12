module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.asio.notification;

#include "details/session_retirement.hxx"

namespace forge::net::p2p::detail {

session_retirement::session_retirement() = default;

session_retirement::~session_retirement() = default;

bool session_retirement::track(session_teardown::ticket ticket) noexcept {
   if (!ticket.active()) {
      return false;
   }

   const auto lock = std::scoped_lock{mutex_};
   if (terminal_ || close_in_flight_ || ticket_.active()) {
      return false;
   }
   ticket_ = std::move(ticket);
   return true;
}

bool session_retirement::tracked() const noexcept {
   const auto lock = std::scoped_lock{mutex_};
   return ticket_.active();
}

bool session_retirement::terminal() const noexcept {
   const auto lock = std::scoped_lock{mutex_};
   return terminal_;
}

session_retirement::close_start session_retirement::begin_close(bool allow_untracked) noexcept {
   {
      const auto lock = std::scoped_lock{mutex_};
      if (terminal_) {
         return close_start::terminal;
      }
      if (close_in_flight_) {
         return close_start::in_flight;
      }
      if (!allow_untracked && !ticket_.active()) {
         return close_start::untracked;
      }
      close_in_flight_ = true;
   }
   return close_start::started;
}

boost::asio::awaitable<void> session_retirement::async_wait_not_in_flight() {
   for (;;) {
      auto observed = forge::asio::notification::epoch_type{};
      {
         const auto lock = std::scoped_lock{mutex_};
         // Sample before releasing the predicate lock so a transition before
         // async_wait subscribes is retained by the notification epoch.
         observed = changed_.epoch();
         if (!close_in_flight_) {
            co_return;
         }
      }
      co_await changed_.async_wait(observed);
   }
}

bool session_retirement::complete_terminal(session_teardown::ticket& ticket) noexcept {
   {
      const auto lock = std::scoped_lock{mutex_};
      if (terminal_) {
         return false;
      }
      terminal_ = true;
      close_in_flight_ = false;
      ticket = std::move(ticket_);
   }
   changed_.notify();
   return true;
}

void session_retirement::quarantine() noexcept {
   auto ticket = session_teardown::ticket{};
   {
      const auto lock = std::scoped_lock{mutex_};
      if (terminal_) {
         return;
      }
      close_in_flight_ = false;
      ticket = std::move(ticket_);
   }
   ticket.release();
   changed_.notify();
}

} // namespace forge::net::p2p::detail
