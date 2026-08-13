module;

#include "details/async_waiter.hxx"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

module forge.asio.notification;

#include "details/notification_impl.hxx"

namespace forge::asio {

notification::epoch_type notification::impl::epoch() const noexcept {
   auto lock = std::scoped_lock{mutex};
   return current_epoch;
}

std::optional<notification::epoch_type>
notification::impl::subscribe(epoch_type observed_epoch,
                              const std::shared_ptr<detail::async_waiter>& value) {
   auto lock = std::scoped_lock{mutex};
   if (current_epoch != observed_epoch) {
      return current_epoch;
   }
   std::erase_if(waiters, [](const auto& waiter) { return waiter.expired(); });
   waiters.emplace_back(value);
   return std::nullopt;
}

void notification::impl::unsubscribe(const std::shared_ptr<detail::async_waiter>& value) noexcept {
   auto lock = std::scoped_lock{mutex};
   std::erase_if(waiters, [&value](const auto& candidate) {
      const auto pending = candidate.lock();
      return !pending || pending == value;
   });
}

boost::asio::awaitable<notification::epoch_type>
notification::impl::async_wait(epoch_type observed_epoch) {
   co_return co_await async_wait_until(observed_epoch, boost::asio::steady_timer::time_point::max());
}

boost::asio::awaitable<notification::epoch_type>
notification::impl::async_wait_until(epoch_type observed_epoch,
                                     std::chrono::steady_clock::time_point deadline) {
   auto executor = co_await boost::asio::this_coro::executor;
   auto pending = std::make_shared<detail::async_waiter>(executor);
   if (const auto current = subscribe(observed_epoch, pending)) {
      co_return *current;
   }

   const auto wait_error = co_await pending->wait_until_cancellable(deadline);
   unsubscribe(pending);

   auto restore_error = boost::system::error_code{};
   co_await boost::asio::dispatch(
       executor,
       boost::asio::bind_cancellation_slot(
           boost::asio::cancellation_slot{},
           boost::asio::redirect_error(boost::asio::use_awaitable, restore_error)));
   (void)restore_error;

   const auto current = epoch();
   if (wait_error && current == observed_epoch) {
      throw boost::system::system_error{wait_error};
   }
   co_return current;
}

void notification::impl::notify() noexcept {
   auto pending = std::vector<std::shared_ptr<detail::async_waiter>>{};
   {
      auto lock = std::scoped_lock{mutex};
      ++current_epoch;
      if (current_epoch == 0) {
         ++current_epoch;
      }
      pending.reserve(waiters.size());
      for (const auto& value : waiters) {
         if (auto waiter = value.lock()) {
            pending.push_back(std::move(waiter));
         }
      }
      waiters.clear();
   }
   for (const auto& waiter : pending) {
      waiter->wake();
   }
}

} // namespace forge::asio
