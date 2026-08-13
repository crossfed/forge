#include "details/async_waiter.hxx"

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <memory>
#include <utility>

namespace forge::asio::detail {

async_waiter::async_waiter(boost::asio::any_io_executor executor)
    : strand_{boost::asio::make_strand(std::move(executor))},
      timer_{strand_, boost::asio::steady_timer::time_point::max()} {}

boost::asio::awaitable<boost::system::error_code> async_waiter::wait() {
   auto error = boost::system::error_code{};
   co_await boost::asio::dispatch(
      strand_,
      boost::asio::bind_cancellation_slot(
         boost::asio::cancellation_slot{}, boost::asio::redirect_error(boost::asio::use_awaitable, error)));
   if (error) {
      co_return error;
   }

   if (notified_) {
      co_return boost::asio::error::operation_aborted;
   }

   co_await timer_.async_wait(boost::asio::bind_cancellation_slot(
      boost::asio::cancellation_slot{}, boost::asio::redirect_error(boost::asio::use_awaitable, error)));
   co_return error;
}

boost::asio::awaitable<boost::system::error_code>
async_waiter::wait_until(std::chrono::steady_clock::time_point deadline) {
   auto error = boost::system::error_code{};
   co_await boost::asio::dispatch(
      strand_,
      boost::asio::bind_cancellation_slot(
         boost::asio::cancellation_slot{}, boost::asio::redirect_error(boost::asio::use_awaitable, error)));
   if (error) {
      co_return error;
   }

   if (notified_) {
      co_return boost::asio::error::operation_aborted;
   }

   timer_.expires_at(deadline);
   co_await timer_.async_wait(boost::asio::bind_cancellation_slot(
      boost::asio::cancellation_slot{}, boost::asio::redirect_error(boost::asio::use_awaitable, error)));
   co_return error;
}

boost::asio::awaitable<boost::system::error_code>
async_waiter::wait_until_cancellable(std::chrono::steady_clock::time_point deadline) {
   auto error = boost::system::error_code{};
   co_await boost::asio::dispatch(
      strand_, boost::asio::redirect_error(boost::asio::use_awaitable, error));
   if (error) {
      co_return error;
   }

   if (notified_) {
      co_return boost::asio::error::operation_aborted;
   }

   timer_.expires_at(deadline);
   co_await timer_.async_wait(
      boost::asio::redirect_error(boost::asio::use_awaitable, error));
   co_return error;
}

void async_waiter::wake() noexcept {
   auto weak = weak_from_this();
   boost::asio::dispatch(strand_, [weak] {
      if (auto self = weak.lock()) {
         try {
            self->notified_ = true;
            self->timer_.cancel();
         } catch (...) {
            // Completion and shutdown paths must stay noexcept.
         }
      }
   });
}

} // namespace forge::asio::detail
