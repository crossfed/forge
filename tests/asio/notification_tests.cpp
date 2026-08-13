#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/system/system_error.hpp>

import forge.asio.notification;
import forge.asio.runtime;

namespace {

boost::asio::awaitable<bool> wait_for_cancellation(
    forge::asio::notification& signal, std::atomic_bool& started) {
   const auto before = co_await boost::asio::this_coro::executor;
   const auto observed = signal.epoch();
   started.store(true, std::memory_order_release);
   auto canceled = false;
   try {
      (void)co_await signal.async_wait(observed);
   } catch (const boost::system::system_error& error) {
      canceled = error.code() == boost::asio::error::operation_aborted;
   }
   const auto after = co_await boost::asio::this_coro::executor;
   co_return canceled && before == after;
}

} // namespace

BOOST_AUTO_TEST_CASE(asio_notification_preserves_late_and_racing_wakes) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto signal = forge::asio::notification{};

   const auto late_epoch = signal.epoch();
   signal.notify();
   auto late = boost::asio::co_spawn(runtime.context(), signal.async_wait(late_epoch), boost::asio::use_future);
   BOOST_REQUIRE(late.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   BOOST_CHECK_NE(late.get(), late_epoch);

   const auto shared_epoch = signal.epoch();
   auto waiters = std::vector<std::future<forge::asio::notification::epoch_type>>{};
   waiters.reserve(64);
   for (auto index = 0; index != 64; ++index) {
      waiters.push_back(
          boost::asio::co_spawn(runtime.context(), signal.async_wait(shared_epoch), boost::asio::use_future));
   }
   signal.notify();
   for (auto& waiter : waiters) {
      BOOST_REQUIRE(waiter.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
      BOOST_CHECK_NE(waiter.get(), shared_epoch);
   }

   for (auto attempt = 0; attempt != 64; ++attempt) {
      const auto observed = signal.epoch();
      auto waiting = boost::asio::co_spawn(runtime.context(), signal.async_wait(observed), boost::asio::use_future);
      auto notifier = std::thread{[&signal] { signal.notify(); }};
      notifier.join();

      BOOST_REQUIRE(waiting.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
      BOOST_CHECK_NE(waiting.get(), observed);
   }
}

BOOST_AUTO_TEST_CASE(asio_notification_cancellation_is_prompt_and_preserves_caller_executor) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto signal = forge::asio::notification{};
   auto caller = boost::asio::make_strand(runtime.context());

   for (auto attempt = 0; attempt != 32; ++attempt) {
      auto started = std::atomic_bool{false};
      auto cancellation = boost::asio::cancellation_signal{};
      auto waiting = boost::asio::co_spawn(
          caller, wait_for_cancellation(signal, started),
          boost::asio::bind_cancellation_slot(cancellation.slot(), boost::asio::use_future));

      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
      while (!started.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
         std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }
      BOOST_REQUIRE(started.load(std::memory_order_acquire));
      cancellation.emit(boost::asio::cancellation_type::all);
      BOOST_REQUIRE(waiting.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
      BOOST_CHECK(waiting.get());
   }

   const auto observed = signal.epoch();
   signal.notify();
   auto successor = boost::asio::co_spawn(caller, signal.async_wait(observed), boost::asio::use_future);
   BOOST_REQUIRE(successor.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   BOOST_CHECK_NE(successor.get(), observed);
}
