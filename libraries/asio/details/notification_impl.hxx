#pragma once

namespace forge::asio {

struct notification::impl final {
   [[nodiscard]] epoch_type epoch() const noexcept;
   boost::asio::awaitable<epoch_type> async_wait(epoch_type observed_epoch);
   boost::asio::awaitable<epoch_type> async_wait_until(
       epoch_type observed_epoch, std::chrono::steady_clock::time_point deadline);
   [[nodiscard]] std::optional<epoch_type> subscribe(
       epoch_type observed_epoch, const std::shared_ptr<detail::async_waiter>& value);
   void unsubscribe(const std::shared_ptr<detail::async_waiter>& value) noexcept;
   void notify() noexcept;

   mutable std::mutex mutex;
   epoch_type current_epoch = 0;
   std::vector<std::weak_ptr<detail::async_waiter>> waiters;
};

} // namespace forge::asio
