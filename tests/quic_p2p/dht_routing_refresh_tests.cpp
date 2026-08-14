module;

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>

module forge.net.p2p.node;

import forge.asio.runtime;
import forge.asio.notification;
import forge.multiformats.multihash;
import forge.multiformats.types;
import forge.net.p2p.dht;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;

#include "../../libraries/net/p2p/details/dht_routing_refresh.hxx"
#include "../../libraries/net/p2p/details/lifecycle_wakeup.hxx"

namespace forge::net::p2p {
namespace {

[[nodiscard]] peer_id refresh_peer(std::uint8_t value) {
   const auto payload = forge::multiformats::bytes{value};
   return peer_id::from_bytes(forge::multiformats::multihash::identity(payload).encode());
}

template <typename Predicate> [[nodiscard]] bool refresh_eventually(Predicate&& predicate) {
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
   while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
         return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
   }
   return true;
}

struct fake_refresh_clock {
   using time_point = detail::dht_routing_refresh::time_point;

   std::atomic<std::int64_t> milliseconds{1};

   [[nodiscard]] time_point now() const noexcept {
      return time_point{std::chrono::milliseconds{milliseconds.load(std::memory_order_acquire)}};
   }

   void advance(std::chrono::milliseconds amount) noexcept {
      milliseconds.fetch_add(amount.count(), std::memory_order_acq_rel);
   }

   [[nodiscard]] detail::dht_routing_refresh::time_source source() {
      return detail::dht_routing_refresh::time_source{
          .now = [this] { return now(); },
          .wait_until = [](std::shared_ptr<detail::lifecycle_wakeup> wakeup, std::uint64_t observed, time_point)
              -> boost::asio::awaitable<std::uint64_t> { co_return co_await wakeup->async_wait(observed); },
      };
   }
};

} // namespace

BOOST_AUTO_TEST_SUITE(dht_routing_refresh_tests)

BOOST_AUTO_TEST_CASE(dht_routing_refresh_uses_fake_time_and_coalesces_early_wakeups) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto local = refresh_peer(1);
   auto routing = dht::routing_table{local};
   routing.upsert(dht::peer{.id = refresh_peer(2)}, dht::routing_admission::verified_server);

   auto clock = fake_refresh_clock{};
   auto queries = std::atomic_size_t{};
   auto refresh = detail::dht_routing_refresh{
       local,
       {detail::dht_routing_refresh::profile{
           .protocol = builtins::kad_dht,
           .routing = &routing,
           .interval = std::chrono::minutes{10},
           .query_timeout = std::chrono::milliseconds{75},
       }},
       [&queries](protocol_id, dht::key, std::chrono::milliseconds timeout) -> boost::asio::awaitable<bool> {
          BOOST_TEST(timeout == std::chrono::milliseconds{75});
          queries.fetch_add(1, std::memory_order_acq_rel);
          co_return true;
       },
       clock.source(),
   };
   auto running = boost::asio::co_spawn(runtime.context(), refresh.async_run(), boost::asio::use_future);

   BOOST_REQUIRE(refresh_eventually([&] { return queries.load(std::memory_order_acquire) > 1; }));
   std::this_thread::sleep_for(std::chrono::milliseconds{20});
   const auto initial = queries.load(std::memory_order_acquire);
   for (auto index = 0; index < 8; ++index) {
      refresh.notify_verified_server();
   }
   std::this_thread::sleep_for(std::chrono::milliseconds{20});
   BOOST_TEST(queries.load(std::memory_order_acquire) == initial);

   clock.advance(std::chrono::minutes{11});
   refresh.notify_verified_server();
   BOOST_REQUIRE(refresh_eventually([&] { return queries.load(std::memory_order_acquire) > initial; }));

   refresh.request_stop();
   BOOST_REQUIRE(running.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   running.get();
}

BOOST_AUTO_TEST_CASE(dht_routing_refresh_fake_time_proves_retry_backoff_and_cancellation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto local = refresh_peer(11);
   auto routing = dht::routing_table{local};
   routing.upsert(dht::peer{.id = refresh_peer(12)}, dht::routing_admission::verified_server);

   auto clock = fake_refresh_clock{};
   auto queries = std::atomic_size_t{};
   auto refresh = detail::dht_routing_refresh{
       local,
       {detail::dht_routing_refresh::profile{
           .protocol = builtins::kad_dht,
           .routing = &routing,
           .interval = std::chrono::minutes{10},
           .query_timeout = std::chrono::milliseconds{125},
       }},
       [&queries](protocol_id, dht::key, std::chrono::milliseconds timeout) -> boost::asio::awaitable<bool> {
          BOOST_TEST(timeout == std::chrono::milliseconds{125});
          queries.fetch_add(1, std::memory_order_acq_rel);
          co_return false;
       },
       clock.source(),
   };
   auto running = boost::asio::co_spawn(runtime.context(), refresh.async_run(), boost::asio::use_future);

   BOOST_REQUIRE(refresh_eventually([&] { return queries.load(std::memory_order_acquire) > 1; }));
   std::this_thread::sleep_for(std::chrono::milliseconds{20});
   const auto failed = queries.load(std::memory_order_acquire);
   clock.advance(std::chrono::milliseconds{500});
   refresh.notify_verified_server();
   std::this_thread::sleep_for(std::chrono::milliseconds{20});
   BOOST_TEST(queries.load(std::memory_order_acquire) == failed);

   clock.advance(std::chrono::seconds{3});
   refresh.notify_verified_server();
   BOOST_REQUIRE(refresh_eventually([&] { return queries.load(std::memory_order_acquire) > failed; }));

   refresh.request_stop();
   BOOST_REQUIRE(running.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   running.get();
}

BOOST_AUTO_TEST_CASE(dht_routing_refresh_status_is_synchronized_with_wakeup_rescheduling) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   const auto local = refresh_peer(21);
   auto routing = dht::routing_table{local};
   routing.upsert(dht::peer{.id = refresh_peer(22)}, dht::routing_admission::verified_server);

   auto clock = fake_refresh_clock{};
   auto queries = std::atomic_size_t{};
   auto refresh = detail::dht_routing_refresh{
       local,
       {detail::dht_routing_refresh::profile{
           .protocol = builtins::kad_dht,
           .routing = &routing,
           .interval = std::chrono::milliseconds{1},
           .query_timeout = std::chrono::milliseconds{250},
       }},
       [&queries](protocol_id, dht::key, std::chrono::milliseconds timeout) -> boost::asio::awaitable<bool> {
          BOOST_TEST(timeout == std::chrono::milliseconds{250});
          queries.fetch_add(1, std::memory_order_acq_rel);
          co_return true;
       },
       clock.source(),
   };
   auto running = boost::asio::co_spawn(runtime.context(), refresh.async_run(), boost::asio::use_future);
   BOOST_REQUIRE(refresh_eventually([&] { return queries.load(std::memory_order_acquire) > 1; }));

   auto missing_status = std::atomic_bool{};
   auto reader = std::jthread{[&] {
      for (auto attempt = 0U; attempt < 10'000U; ++attempt) {
         if (!refresh.status(builtins::kad_dht)) {
            missing_status.store(true, std::memory_order_release);
         }
      }
   }};
   for (auto attempt = 0U; attempt < 1'000U; ++attempt) {
      clock.advance(std::chrono::milliseconds{2});
      refresh.notify_verified_server();
      static_cast<void>(refresh.status(builtins::kad_dht));
   }
   reader.join();

   BOOST_TEST(!missing_status.load(std::memory_order_acquire));
   refresh.request_stop();
   BOOST_REQUIRE(running.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   running.get();
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace forge::net::p2p
