module;

#include <boost/test/unit_test.hpp>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.asio.runtime;

#include "../../libraries/net/p2p/details/session_teardown.hxx"

namespace forge::net::p2p {
namespace {

BOOST_AUTO_TEST_CASE(p2p_session_teardown_waits_for_started_transport_cleanup) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto release =
       std::make_shared<boost::asio::steady_timer>(runtime.context(), boost::asio::steady_timer::time_point::max());
   auto close_started = std::atomic_size_t{0};
   auto cancel_called = std::atomic_size_t{0};
   auto teardown = detail::session_teardown{runtime.context().get_executor()};

   auto operations = std::vector<detail::session_teardown::operation>{};
   for (auto remaining = 2U; remaining != 0U; --remaining) {
      operations.push_back(detail::session_teardown::operation{
          .close = [release, &close_started]() -> boost::asio::awaitable<void> {
             close_started.fetch_add(1, std::memory_order_release);
             auto error = boost::system::error_code{};
             co_await release->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
          },
          .cancel = [&cancel_called] { cancel_called.fetch_add(1, std::memory_order_release); },
      });
   }
   teardown.start(std::move(operations));

   const auto close_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (close_started.load(std::memory_order_acquire) != 2U) {
      BOOST_REQUIRE(std::chrono::steady_clock::now() < close_deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
   }

   auto stopped = boost::asio::co_spawn(runtime.context(), teardown.wait(), boost::asio::use_future);
   const auto waiting_for_cleanup = stopped.wait_for(std::chrono::milliseconds{50}) == std::future_status::timeout;
   BOOST_TEST(waiting_for_cleanup);

   boost::asio::post(runtime.context(), [release] { release->cancel(); });
   const auto cleanup_completed = stopped.wait_for(std::chrono::seconds{2}) == std::future_status::ready;
   BOOST_REQUIRE(cleanup_completed);
   stopped.get();
   BOOST_TEST(cancel_called.load(std::memory_order_acquire) == 0U);
}

} // namespace
} // namespace forge::net::p2p
