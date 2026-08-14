module;

#include <boost/test/unit_test.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <atomic>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.asio.blocking;
import forge.asio.gate;
import forge.asio.runtime;
import forge.net.p2p.dht;
import forge.net.p2p.exceptions;
import forge.net.p2p.protocol;

#include "../../libraries/net/p2p/details/dht_provider_registry.hxx"

namespace forge::net::p2p {

BOOST_AUTO_TEST_SUITE(dht_provider_registry_tests)

BOOST_AUTO_TEST_CASE(dht_provider_renewal_is_clamped_to_stamped_expiry_budget) {
   const auto stamped_at = std::chrono::steady_clock::time_point{std::chrono::seconds{100}};
   const auto renewal = detail::dht_provider_registry::schedule{
       .provider_ttl = std::chrono::seconds{90},
       .address_ttl = std::chrono::seconds{60},
       .republish_interval = std::chrono::seconds{59},
   };

   const auto clamped = detail::dht_provider_renewal_deadline(stamped_at, renewal, std::chrono::seconds{10},
                                                              std::chrono::milliseconds{61'950});
   BOOST_CHECK(clamped == stamped_at + std::chrono::seconds{50});

   const auto nominal =
       detail::dht_provider_renewal_deadline(stamped_at, renewal, std::chrono::seconds{10}, std::chrono::seconds{30});
   BOOST_CHECK(nominal == stamped_at + std::chrono::seconds{30});

   const auto saturated = detail::dht_provider_renewal_deadline(
       (std::chrono::steady_clock::time_point::max)() - std::chrono::milliseconds{5}, renewal, std::chrono::seconds{10},
       std::chrono::seconds{30});
   BOOST_CHECK(saturated == (std::chrono::steady_clock::time_point::max)());
}

BOOST_AUTO_TEST_CASE(dht_provider_retry_is_paced_and_only_clamped_by_a_future_expiry_budget) {
   const auto now = std::chrono::steady_clock::time_point{std::chrono::seconds{100}};
   const auto renewal = detail::dht_provider_registry::schedule{
       .provider_ttl = std::chrono::seconds{60},
       .address_ttl = std::chrono::seconds{60},
       .republish_interval = std::chrono::seconds{20},
   };

   const auto paced = detail::dht_provider_retry_deadline(now, now - std::chrono::seconds{10}, renewal,
                                                          std::chrono::seconds{5}, std::chrono::seconds{3});
   BOOST_CHECK(paced == now + std::chrono::seconds{3});

   const auto clamped = detail::dht_provider_retry_deadline(now, now - std::chrono::seconds{50}, renewal,
                                                            std::chrono::seconds{5}, std::chrono::seconds{10});
   BOOST_CHECK(clamped == now + std::chrono::seconds{5});

   const auto expired = detail::dht_provider_retry_deadline(now, now - std::chrono::seconds{70}, renewal,
                                                            std::chrono::seconds{5}, std::chrono::seconds{3});
   BOOST_CHECK(expired == now + std::chrono::seconds{3});
}

BOOST_AUTO_TEST_CASE(dht_provider_strengthening_waits_for_background_publication) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto blocked = std::make_shared<forge::asio::gate>();
   auto blocked_ticket = forge::asio::blocking::run(runtime, blocked->acquire());
   auto background_entered = std::promise<void>{};
   auto background_entered_future = background_entered.get_future();
   auto strengthened_entered = std::promise<void>{};
   auto strengthened_entered_future = strengthened_entered.get_future();
   auto calls = std::atomic_size_t{};
   auto observed_quorums = std::vector<std::size_t>{};
   auto observed_mutex = std::mutex{};

   auto registry = std::make_shared<detail::dht_provider_registry>(detail::dht_provider_registry::callbacks{
       .track = [] { return std::make_shared<int>(1); },
       .launch =
           [&runtime](std::function<boost::asio::awaitable<void>()> task) {
              boost::asio::co_spawn(runtime.context(), task(), boost::asio::detached);
              return true;
           },
       .prepare = [](protocol_id, dht::key, detail::dht_provider_registry::schedule)
           -> boost::asio::awaitable<detail::dht_provider_registry::prepared_provider> {
          co_return detail::dht_provider_registry::prepared_provider{.provider = {},
                                                                     .stamped_at = std::chrono::steady_clock::now()};
       },
       .publish = [&](protocol_id, dht::key, dht::peer,
                      dht::query_options query) -> boost::asio::awaitable<std::size_t> {
          const auto call = calls.fetch_add(1, std::memory_order_acq_rel) + 1;
          {
             const auto lock = std::scoped_lock{observed_mutex};
             observed_quorums.push_back(query.quorum);
          }
          if (call == 2) {
             background_entered.set_value();
             auto ticket = co_await blocked->acquire();
          } else if (call == 3) {
             strengthened_entered.set_value();
          }
          co_return query.quorum;
       },
       .remove = [](protocol_id, dht::key) -> boost::asio::awaitable<void> { co_return; },
       .publication_limit = [](const protocol_id&) { return std::size_t{4}; },
   });
   registry->open_admission();
   const auto renewal = detail::dht_provider_registry::schedule{
       .provider_ttl = std::chrono::seconds{90},
       .address_ttl = std::chrono::seconds{60},
       .republish_interval = std::chrono::seconds{30},
   };
   auto first = forge::asio::blocking::run(
       runtime, registry->async_acquire(protocol_id{.value = "/forge/test/kad/1.0.0"}, dht::key{.bytes = {1}},
                                        dht::query_options{.requested_count = 1, .quorum = 1}, renewal));

   registry->notify_endpoints_changed();
   BOOST_REQUIRE(background_entered_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   auto strengthened = boost::asio::co_spawn(
       runtime.context(),
       registry->async_acquire(protocol_id{.value = "/forge/test/kad/1.0.0"}, dht::key{.bytes = {1}},
                               dht::query_options{.requested_count = 2, .quorum = 2}, renewal),
       boost::asio::use_future);
   BOOST_CHECK(strengthened_entered_future.wait_for(std::chrono::milliseconds{100}) != std::future_status::ready);

   blocked_ticket.release();
   BOOST_REQUIRE(strengthened.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   auto second = strengthened.get();
   BOOST_REQUIRE(strengthened_entered_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   {
      const auto lock = std::scoped_lock{observed_mutex};
      BOOST_REQUIRE_EQUAL(observed_quorums.size(), 3U);
      BOOST_TEST(observed_quorums == (std::vector<std::size_t>{1, 1, 2}));
   }

   registry->seal();
   forge::asio::blocking::run(runtime, registry->async_drain());
}

BOOST_AUTO_TEST_CASE(dht_provider_failed_republish_retries_before_stamped_expiry) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto second_finished = std::promise<void>{};
   auto second_finished_future = second_finished.get_future();
   auto third_entered = std::promise<void>{};
   auto third_entered_future = third_entered.get_future();
   auto calls = std::atomic_size_t{};

   auto registry = std::make_shared<detail::dht_provider_registry>(detail::dht_provider_registry::callbacks{
       .track = [] { return std::make_shared<int>(1); },
       .launch =
           [&runtime](std::function<boost::asio::awaitable<void>()> task) {
              boost::asio::co_spawn(runtime.context(), task(), boost::asio::detached);
              return true;
           },
       .prepare = [](protocol_id, dht::key, detail::dht_provider_registry::schedule)
           -> boost::asio::awaitable<detail::dht_provider_registry::prepared_provider> {
          co_return detail::dht_provider_registry::prepared_provider{.provider = {},
                                                                     .stamped_at = std::chrono::steady_clock::now()};
       },
       .publish = [&](protocol_id, dht::key, dht::peer,
                      dht::query_options query) -> boost::asio::awaitable<std::size_t> {
          const auto call = calls.fetch_add(1, std::memory_order_acq_rel) + 1;
          if (call == 2) {
             auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
             timer.expires_after(std::chrono::milliseconds{200});
             co_await timer.async_wait(boost::asio::use_awaitable);
             second_finished.set_value();
             co_return 0;
          }
          if (call == 3) {
             third_entered.set_value();
          }
          co_return query.quorum;
       },
       .remove = [](protocol_id, dht::key) -> boost::asio::awaitable<void> { co_return; },
       .publication_limit = [](const protocol_id&) { return std::size_t{4}; },
   });
   registry->open_admission();
   const auto renewal = detail::dht_provider_registry::schedule{
       .provider_ttl = std::chrono::seconds{5},
       .address_ttl = std::chrono::seconds{5},
       .republish_interval = std::chrono::seconds{2},
   };
   auto registration = forge::asio::blocking::run(
       runtime,
       registry->async_acquire(
           protocol_id{.value = "/forge/test/kad/1.0.0"}, dht::key{.bytes = {2}},
           dht::query_options{.requested_count = 1, .quorum = 1, .timeout = std::chrono::seconds{1}}, renewal));

   BOOST_REQUIRE(second_finished_future.wait_for(std::chrono::seconds{3}) == std::future_status::ready);
   BOOST_CHECK(third_entered_future.wait_for(std::chrono::milliseconds{300}) != std::future_status::ready);
   BOOST_REQUIRE(third_entered_future.wait_for(std::chrono::milliseconds{2'500}) == std::future_status::ready);

   registry->seal();
   forge::asio::blocking::run(runtime, registry->async_drain());
}

BOOST_AUTO_TEST_CASE(dht_provider_prepare_failure_retries_against_last_successful_publication) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto second_failed = std::promise<void>{};
   auto second_failed_future = second_failed.get_future();
   auto third_entered = std::promise<void>{};
   auto third_entered_future = third_entered.get_future();
   auto calls = std::atomic_size_t{};

   auto registry = std::make_shared<detail::dht_provider_registry>(detail::dht_provider_registry::callbacks{
       .track = [] { return std::make_shared<int>(1); },
       .launch =
           [&runtime](std::function<boost::asio::awaitable<void>()> task) {
              boost::asio::co_spawn(runtime.context(), task(), boost::asio::detached);
              return true;
           },
       .prepare = [&](protocol_id, dht::key, detail::dht_provider_registry::schedule)
           -> boost::asio::awaitable<detail::dht_provider_registry::prepared_provider> {
          const auto call = calls.fetch_add(1, std::memory_order_acq_rel) + 1;
          if (call == 2) {
             second_failed.set_value();
             throw std::runtime_error{"injected provider prepare failure"};
          }
          if (call == 3) {
             third_entered.set_value();
          }
          co_return detail::dht_provider_registry::prepared_provider{.provider = {},
                                                                     .stamped_at = std::chrono::steady_clock::now()};
       },
       .publish = [](protocol_id, dht::key, dht::peer,
                     dht::query_options query) -> boost::asio::awaitable<std::size_t> { co_return query.quorum; },
       .remove = [](protocol_id, dht::key) -> boost::asio::awaitable<void> { co_return; },
       .publication_limit = [](const protocol_id&) { return std::size_t{4}; },
   });
   registry->open_admission();
   const auto renewal = detail::dht_provider_registry::schedule{
       .provider_ttl = std::chrono::seconds{5},
       .address_ttl = std::chrono::seconds{5},
       .republish_interval = std::chrono::seconds{2},
   };
   auto registration = forge::asio::blocking::run(
       runtime,
       registry->async_acquire(
           protocol_id{.value = "/forge/test/kad/1.0.0"}, dht::key{.bytes = {3}},
           dht::query_options{.requested_count = 1, .quorum = 1, .timeout = std::chrono::seconds{1}}, renewal));

   BOOST_REQUIRE(second_failed_future.wait_for(std::chrono::seconds{3}) == std::future_status::ready);
   BOOST_CHECK(third_entered_future.wait_for(std::chrono::milliseconds{300}) != std::future_status::ready);
   BOOST_REQUIRE(third_entered_future.wait_for(std::chrono::milliseconds{2'500}) == std::future_status::ready);

   registry->seal();
   forge::asio::blocking::run(runtime, registry->async_drain());
}

BOOST_AUTO_TEST_CASE(dht_provider_removal_serializes_with_strengthened_publication) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto blocked = std::make_shared<forge::asio::gate>();
   auto blocked_ticket = forge::asio::blocking::run(runtime, blocked->acquire());
   auto strengthening_entered = std::promise<void>{};
   auto strengthening_entered_future = strengthening_entered.get_future();
   auto removal_entered = std::promise<void>{};
   auto removal_entered_future = removal_entered.get_future();
   auto prepare_calls = std::atomic_size_t{};
   auto durable_owned = std::atomic_bool{false};

   auto registry = std::make_shared<detail::dht_provider_registry>(detail::dht_provider_registry::callbacks{
       .track = [] { return std::make_shared<int>(1); },
       .launch =
           [&runtime](std::function<boost::asio::awaitable<void>()> task) {
              boost::asio::co_spawn(runtime.context(), task(), boost::asio::detached);
              return true;
           },
       .prepare = [&](protocol_id, dht::key, detail::dht_provider_registry::schedule)
           -> boost::asio::awaitable<detail::dht_provider_registry::prepared_provider> {
          if (prepare_calls.fetch_add(1, std::memory_order_acq_rel) == 1) {
             strengthening_entered.set_value();
             auto ticket = co_await blocked->acquire();
          }
          durable_owned.store(true, std::memory_order_release);
          co_return detail::dht_provider_registry::prepared_provider{.provider = {},
                                                                     .stamped_at = std::chrono::steady_clock::now()};
       },
       .publish = [](protocol_id, dht::key, dht::peer,
                     dht::query_options query) -> boost::asio::awaitable<std::size_t> { co_return query.quorum; },
       .remove = [&](protocol_id, dht::key) -> boost::asio::awaitable<void> {
          durable_owned.store(false, std::memory_order_release);
          removal_entered.set_value();
          co_return;
       },
       .publication_limit = [](const protocol_id&) { return std::size_t{4}; },
   });
   registry->open_admission();
   const auto renewal = detail::dht_provider_registry::schedule{
       .provider_ttl = std::chrono::seconds{90},
       .address_ttl = std::chrono::seconds{60},
       .republish_interval = std::chrono::seconds{30},
   };
   auto first = forge::asio::blocking::run(
       runtime, registry->async_acquire(protocol_id{.value = "/forge/test/kad/1.0.0"}, dht::key{.bytes = {4}},
                                        dht::query_options{.requested_count = 1, .quorum = 1}, renewal));
   auto strengthened = boost::asio::co_spawn(
       runtime.context(),
       registry->async_acquire(protocol_id{.value = "/forge/test/kad/1.0.0"}, dht::key{.bytes = {4}},
                               dht::query_options{.requested_count = 2, .quorum = 2}, renewal),
       boost::asio::use_future);
   BOOST_REQUIRE(strengthening_entered_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);

   registry->seal();
   auto drain = boost::asio::co_spawn(runtime.context(), registry->async_drain(), boost::asio::use_future);
   BOOST_CHECK(removal_entered_future.wait_for(std::chrono::milliseconds{100}) != std::future_status::ready);
   blocked_ticket.release();

   BOOST_REQUIRE(strengthened.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   try {
      static_cast<void>(strengthened.get());
      BOOST_FAIL("expected strengthened provider admission to observe sealed registry");
   } catch (const forge::exceptions::base& error) {
      BOOST_TEST(exceptions::is(error, exceptions::code::closed));
   }
   BOOST_REQUIRE(removal_entered_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   BOOST_REQUIRE(drain.wait_for(std::chrono::seconds{2}) == std::future_status::ready);
   drain.get();
   BOOST_TEST(!durable_owned.load(std::memory_order_acquire));
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace forge::net::p2p
