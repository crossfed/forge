#include <boost/test/unit_test.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/system/error_code.hpp>

#include "../fixtures/local_dns_server.hxx"

import forge.asio.blocking;
import forge.asio.runtime;
import forge.net.dns.exceptions;
import forge.net.dns.resolver;
import forge.net.dns.types;

#include "details/nameserver_formatter.hxx"

namespace {

namespace asio = boost::asio;
namespace dns = forge::net::dns;
using bytes = std::vector<std::uint8_t>;
using udp = asio::ip::udp;
using forge::tests::dns::encoded_name;
using forge::tests::dns::local_dns_server;
using forge::tests::dns::make_failure_response;
using forge::tests::dns::make_response;
using forge::tests::dns::parse_question;
using forge::tests::dns::response_answer;

[[nodiscard]] std::optional<bytes> response_for(const std::uint8_t* request, std::size_t size) {
   const auto question = parse_question(request, size);
   if (!question || question->name == "stall.test") {
      return std::nullopt;
   }
   if (question->name == "partial.test" && question->type == 28) {
      return make_failure_response(request, *question, 2);
   }
   if (question->name == "hard-failure.test" && question->type == 28) {
      return make_failure_response(request, *question, 1);
   }

   auto answers = std::vector<response_answer>{};
   if ((question->name == "a.test" || question->name == "dual.test" || question->name == "partial.test" ||
        question->name == "hard-failure.test") &&
       question->type == 1) {
      answers.push_back({.type = 1, .value = {192, 0, 2, 7}});
      if (question->name == "dual.test") {
         answers.push_back({.type = 28, .value = {0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0,
                                               0,    0,    0,    0,    0, 0, 0, 2}});
      }
   } else if ((question->name == "aaaa.test" || question->name == "dual.test") && question->type == 28) {
      answers.push_back({.type = 28, .value = {0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0,
                                            0,    0,    0,    0,    0, 0, 0, 1}});
      if (question->name == "dual.test") {
         answers.push_back({.type = 1, .value = {192, 0, 2, 8}});
      }
   } else if (question->name == "txt.test" && question->type == 16) {
      answers.push_back({.type = 16, .value = {3, 'f', 'o', 'o', 3, 'b', 'a', 'r'}});
   } else if (question->name == "poison-a.test" && question->type == 1) {
      answers.push_back({.type = 1, .value = {192, 0, 2, 20}, .owner = "unrelated.test"});
   } else if (question->name == "poison-aaaa.test" && question->type == 28) {
      answers.push_back({.type = 28,
                         .value = {0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0,
                                   0,    0,    0,    0,    0, 0, 0, 20},
                         .owner = "unrelated.test"});
   } else if (question->name == "poison-txt.test" && question->type == 16) {
      answers.push_back({.type = 16, .value = {6, 'p', 'o', 'i', 's', 'o', 'n'}, .owner = "unrelated.test"});
   } else if (question->name == "many.test" && question->type == 1) {
      answers.push_back({.type = 1, .value = {192, 0, 2, 1}});
      answers.push_back({.type = 1, .value = {192, 0, 2, 2}});
   } else if (question->name == "cname.test" && question->type == 1) {
      answers.push_back({.type = 5, .value = encoded_name("alias.test")});
      answers.push_back({.type = 1, .value = {192, 0, 2, 9}});
   } else if (question->name == "cname-chain.test" && question->type == 1) {
      answers.push_back({.type = 5, .value = encoded_name("alias.cname-chain.test")});
      answers.push_back({.type = 5,
                         .value = encoded_name("terminal.cname-chain.test"),
                         .owner = "alias.cname-chain.test"});
      answers.push_back({.type = 1, .value = {192, 0, 2, 10}, .owner = "terminal.cname-chain.test"});
      answers.push_back({.type = 1, .value = {192, 0, 2, 11}});
      answers.push_back({.type = 1, .value = {192, 0, 2, 12}, .owner = "alias.cname-chain.test"});
   } else if (question->name == "cname-conflict.test" && question->type == 1) {
      answers.push_back({.type = 5, .value = encoded_name("first.cname-conflict.test")});
      answers.push_back({.type = 5, .value = encoded_name("second.cname-conflict.test")});
   } else if (question->name == "cname-cycle.test" && question->type == 1) {
      answers.push_back({.type = 5, .value = encoded_name("alias.cname-cycle.test")});
      answers.push_back({.type = 5,
                         .value = encoded_name("cname-cycle.test"),
                         .owner = "alias.cname-cycle.test"});
   } else if (question->name == "cname-overflow.test" && question->type == 1) {
      for (auto index = 0; index < 9; ++index) {
         const auto suffix = std::to_string(index);
         answers.push_back({.type = 5,
                            .value = encoded_name("target-" + suffix + ".test"),
                            .owner = "unrelated-" + suffix + ".test"});
      }
   } else if (question->name == "cname-bytes.test" && question->type == 1) {
      answers.push_back({.type = 5,
                         .value = encoded_name("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.cname-bytes.test"),
                         .owner = "unrelated-bytes.test"});
   }
   return make_response(request, *question, answers);
}

[[nodiscard]] dns::resolver make_resolver(forge::asio::runtime& runtime, const local_dns_server& server,
                                           std::size_t max_in_flight = 16) {
   return dns::resolver{
       runtime.context().get_executor(),
       {.nameservers = {{.address = "127.0.0.1", .port = server.port()}}, .max_in_flight = max_in_flight},
   };
}

[[nodiscard]] bool has_code(const forge::exceptions::base& error, dns::exceptions::code expected) {
   return dns::exceptions::is(error, expected);
}

boost::asio::awaitable<void> wait_for_active_queries(const dns::resolver& resolver) {
   const auto executor = co_await asio::this_coro::executor;
   while (resolver.active_queries() == 0) {
      auto timer = asio::steady_timer{executor};
      timer.expires_after(std::chrono::milliseconds{1});
      auto error = boost::system::error_code{};
      co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, error));
   }
}

boost::asio::awaitable<void> wait_for_no_active_queries(const dns::resolver& resolver) {
   const auto executor = co_await asio::this_coro::executor;
   while (resolver.active_queries() != 0) {
      auto timer = asio::steady_timer{executor};
      timer.expires_after(std::chrono::milliseconds{1});
      auto error = boost::system::error_code{};
      co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, error));
   }
}

boost::asio::awaitable<void> wait_for_flag(const std::atomic_bool& flag) {
   const auto executor = co_await asio::this_coro::executor;
   while (!flag.load(std::memory_order_acquire)) {
      auto timer = asio::steady_timer{executor};
      timer.expires_after(std::chrono::milliseconds{1});
      auto error = boost::system::error_code{};
      co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, error));
   }
}

BOOST_AUTO_TEST_CASE(resolves_typed_a_aaaa_and_txt_records) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server);

   const auto a = forge::asio::blocking::run(
       runtime, resolver.async_resolve_addresses("a.test", dns::address_family::ipv4));
   BOOST_REQUIRE_EQUAL(a.answers.size(), 1U);
   BOOST_CHECK(a.answers.front().value == asio::ip::make_address("192.0.2.7"));
   BOOST_CHECK_EQUAL(a.answers.front().ttl.count(), 60);

   const auto aaaa = forge::asio::blocking::run(
       runtime, resolver.async_resolve_addresses("aaaa.test", dns::address_family::ipv6));
   BOOST_REQUIRE_EQUAL(aaaa.answers.size(), 1U);
   BOOST_CHECK(aaaa.answers.front().value == asio::ip::make_address("2001:db8::1"));

   const auto dual = forge::asio::blocking::run(
       runtime, resolver.async_resolve_addresses("dual.test", dns::address_family::any));
   BOOST_REQUIRE_EQUAL(dual.answers.size(), 2U);
   BOOST_CHECK(dual.answers[0].value.is_v4() || dual.answers[1].value.is_v4());
   BOOST_CHECK(dual.answers[0].value.is_v6() || dual.answers[1].value.is_v6());

   const auto partial = forge::asio::blocking::run(
       runtime, resolver.async_resolve_addresses("partial.test", dns::address_family::any));
   BOOST_REQUIRE_EQUAL(partial.answers.size(), 1U);
   BOOST_CHECK(partial.answers.front().value == asio::ip::make_address("192.0.2.7"));

   const auto text = forge::asio::blocking::run(runtime, resolver.async_resolve_txt("txt.test"));
   BOOST_REQUIRE_EQUAL(text.answers.size(), 1U);
   const auto expected = bytes{'f', 'o', 'o', 'b', 'a', 'r'};
   BOOST_CHECK_EQUAL_COLLECTIONS(text.answers.front().value.begin(), text.answers.front().value.end(), expected.begin(),
                                 expected.end());
   BOOST_CHECK_EQUAL(text.answers.front().ttl.count(), 60);

   forge::asio::blocking::run(runtime, resolver.async_close());
   forge::asio::blocking::run(runtime, resolver.async_close());
}

BOOST_AUTO_TEST_CASE(local_dns_server_close_and_destruction_join_in_flight_handler) {
   for (const auto destroy : {false, true}) {
      auto entered = std::make_shared<std::promise<void>>();
      auto entered_future = entered->get_future();
      auto release = std::promise<void>{};
      auto released = release.get_future().share();
      auto handler_released = std::make_shared<std::atomic_bool>(false);
      auto lifetime = std::make_shared<int>(0);
      auto weak_lifetime = std::weak_ptr<int>{lifetime};
      auto server = std::make_unique<local_dns_server>(
          [entered, released, handler_released, lifetime](const std::uint8_t* request,
                                                         std::size_t size) -> std::optional<bytes> {
             entered->set_value();
             handler_released->store(released.wait_for(std::chrono::seconds{5}) == std::future_status::ready,
                                     std::memory_order_release);
             return response_for(request, size);
          });
      lifetime.reset();
      auto context = asio::io_context{};
      auto client = udp::socket{context, udp::endpoint{asio::ip::address_v4::loopback(), 0}};
      auto query = bytes{0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
      const auto name = encoded_name("a.test");
      query.insert(query.end(), name.begin(), name.end());
      query.insert(query.end(), {0, 1, 0, 1});
      client.send_to(asio::buffer(query), udp::endpoint{asio::ip::address_v4::loopback(), server->port()});
      const auto handler_entered = entered_future.wait_for(std::chrono::seconds{2}) == std::future_status::ready;

      auto closing = std::promise<void>{};
      auto closing_future = closing.get_future();
      auto closed = std::promise<void>{};
      auto closed_future = closed.get_future();
      auto closer = std::thread{[server = std::move(server), destroy, closing = std::move(closing),
                                 closed = std::move(closed)]() mutable {
         closing.set_value();
         try {
            if (!destroy) {
               server->close();
               server->close();
            }
            server.reset();
            closed.set_value();
         } catch (...) {
            closed.set_exception(std::current_exception());
         }
      }};
      closing_future.wait();
      const auto close_blocked = closed_future.wait_for(std::chrono::milliseconds{20}) == std::future_status::timeout;
      release.set_value();
      closer.join();

      BOOST_TEST(handler_entered);
      BOOST_TEST(close_blocked);
      BOOST_TEST(handler_released->load(std::memory_order_acquire));
      BOOST_CHECK_NO_THROW(closed_future.get());
      BOOST_TEST(weak_lifetime.expired());
   }
}

BOOST_AUTO_TEST_CASE(fails_closed_when_local_dns_response_exceeds_limits) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server);

   auto answer_limit = dns::query_options{};
   answer_limit.max_answers = 1;
   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("many.test", dns::address_family::ipv4,
                                                                               answer_limit)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::resource_limit); });

   auto cname_limit = dns::query_options{};
   cname_limit.max_cnames = 0;
   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("cname.test", dns::address_family::ipv4,
                                                                               cname_limit)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::resource_limit); });

   auto record_limit = dns::query_options{};
   record_limit.max_record_bytes = 3;
   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("a.test", dns::address_family::ipv4,
                                                                               record_limit)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::resource_limit); });

   auto total_limit = dns::query_options{};
   total_limit.max_total_answer_bytes = 5;
   BOOST_CHECK_EXCEPTION(forge::asio::blocking::run(runtime, resolver.async_resolve_txt("txt.test", total_limit)),
                             forge::exceptions::base,
                             [](const auto& error) { return has_code(error, dns::exceptions::code::resource_limit); });

   auto aggregate_answer_limit = dns::query_options{};
   aggregate_answer_limit.max_answers = 1;
   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("dual.test", dns::address_family::any,
                                                                               aggregate_answer_limit)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::resource_limit); });

   forge::asio::blocking::run(runtime, resolver.async_close());
}

BOOST_AUTO_TEST_CASE(address_family_codec_failure_is_hard_even_when_the_other_family_is_usable) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server);

   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime,
                                  resolver.async_resolve_addresses("hard-failure.test", dns::address_family::any)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::codec_error); });
   forge::asio::blocking::run(runtime, resolver.async_close());
}

BOOST_AUTO_TEST_CASE(cname_preflight_rejects_unrelated_count_and_bytes_before_materialization) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server);

   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(
           runtime, resolver.async_resolve_addresses("cname-overflow.test", dns::address_family::ipv4)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::resource_limit); });

   auto byte_limit = dns::query_options{};
   byte_limit.max_cnames = 8;
   byte_limit.max_record_bytes = 128;
   byte_limit.max_total_answer_bytes = 64;
   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime,
                                  resolver.async_resolve_addresses("cname-bytes.test", dns::address_family::ipv4,
                                                                   byte_limit)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::resource_limit); });

   forge::asio::blocking::run(runtime, resolver.async_close());
}

BOOST_AUTO_TEST_CASE(rejects_new_operations_when_max_in_flight_is_reached) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server, 1);
   auto completion = std::make_shared<std::atomic_bool>(false);

   asio::co_spawn(
       runtime.context(), [&resolver, completion]() -> asio::awaitable<void> {
          try {
             static_cast<void>(co_await resolver.async_resolve_addresses("stall.test", dns::address_family::ipv4));
          } catch (...) {
          }
          completion->store(true, std::memory_order_release);
       },
       asio::detached);
   forge::asio::blocking::run(runtime, wait_for_active_queries(resolver));

   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("a.test", dns::address_family::ipv4)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::resource_limit); });

   forge::asio::blocking::run(runtime, resolver.async_close());
   BOOST_CHECK(completion->load(std::memory_order_acquire));
}

BOOST_AUTO_TEST_CASE(rejects_synchronous_invalid_queries_without_leaking_registration) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server);

   BOOST_CHECK_EXCEPTION(forge::asio::blocking::run(
                             runtime, resolver.async_resolve_addresses("", dns::address_family::ipv4)),
                         forge::exceptions::base,
                         [](const auto& error) { return has_code(error, dns::exceptions::code::invalid_options); });
   BOOST_CHECK_EQUAL(resolver.active_queries(), 0U);

   const auto recovered = forge::asio::blocking::run(
       runtime, resolver.async_resolve_addresses("a.test", dns::address_family::ipv4));
   BOOST_CHECK_EQUAL(recovered.answers.size(), 1U);
   forge::asio::blocking::run(runtime, resolver.async_close());
}

BOOST_AUTO_TEST_CASE(cares_synchronous_name_rejection_drains_registered_callback_and_recovers) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server, 1);

   // `a..b` passes the product's non-empty input gate but c-ares rejects it while submitting the query.
   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("a..b", dns::address_family::ipv4)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::internal); });
   BOOST_REQUIRE(forge::asio::blocking::run_for(runtime, wait_for_no_active_queries(resolver), std::chrono::seconds{1}));

   const auto recovered = forge::asio::blocking::run(
       runtime, resolver.async_resolve_addresses("a.test", dns::address_family::ipv4));
   BOOST_REQUIRE_EQUAL(recovered.answers.size(), 1U);
   BOOST_REQUIRE(forge::asio::blocking::run_for(runtime, wait_for_no_active_queries(resolver), std::chrono::seconds{1}));
   forge::asio::blocking::run(runtime, resolver.async_close());
}

BOOST_AUTO_TEST_CASE(rejects_unreachable_rr_owners_and_only_uses_terminal_cname_owner) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server);

   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("poison-a.test", dns::address_family::ipv4)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::not_found); });
   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime,
                                  resolver.async_resolve_addresses("poison-aaaa.test", dns::address_family::ipv6)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::not_found); });
   BOOST_CHECK_EXCEPTION(forge::asio::blocking::run(runtime, resolver.async_resolve_txt("poison-txt.test")),
                         forge::exceptions::base,
                         [](const auto& error) { return has_code(error, dns::exceptions::code::not_found); });

   const auto chained = forge::asio::blocking::run(
       runtime, resolver.async_resolve_addresses("cname-chain.test", dns::address_family::ipv4));
   BOOST_REQUIRE_EQUAL(chained.answers.size(), 1U);
   BOOST_CHECK(chained.answers.front().value == asio::ip::make_address("192.0.2.10"));
   BOOST_REQUIRE_EQUAL(chained.canonical_names.size(), 2U);
   BOOST_CHECK_EQUAL(chained.canonical_names[0], "alias.cname-chain.test");
   BOOST_CHECK_EQUAL(chained.canonical_names[1], "terminal.cname-chain.test");

   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(
           runtime, resolver.async_resolve_addresses("cname-conflict.test", dns::address_family::ipv4)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::codec_error); });
   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("cname-cycle.test", dns::address_family::ipv4)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::codec_error); });

   forge::asio::blocking::run(runtime, resolver.async_close());
}

BOOST_AUTO_TEST_CASE(validates_link_local_nameserver_scope_and_formats_cares_csv) {
   auto runtime = forge::asio::runtime{};
   const auto scoped = dns::resolver_options{
       .nameservers = {{.address = "fe80::1", .port = 5353, .scope_id = "en0"},
                       {.address = "127.0.0.1", .port = 53, .scope_id = ""}},
   };
   const auto views = std::array{
       dns::detail::nameserver_format_view{.address = "fe80::1", .port = 5353, .scope_id = "en0", .ipv6 = true},
       dns::detail::nameserver_format_view{.address = "127.0.0.1", .port = 53, .ipv6 = false},
   };
   BOOST_CHECK_EQUAL(dns::detail::format_nameserver_csv(views), "[fe80::1]:5353%en0,127.0.0.1:53");

   auto valid = dns::resolver{runtime.context().get_executor(), scoped};
   forge::asio::blocking::run(runtime, valid.async_close());

   const auto rejects = [&runtime](dns::nameserver server) {
      return [&runtime, server = std::move(server)] {
         static_cast<void>(dns::resolver{runtime.context().get_executor(), {.nameservers = {server}}});
      };
   };
   BOOST_CHECK_EXCEPTION(rejects({.address = "fe80::1", .port = 53, .scope_id = ""})(), forge::exceptions::base,
                         [](const auto& error) { return has_code(error, dns::exceptions::code::invalid_options); });
   BOOST_CHECK_EXCEPTION(rejects({.address = "2001:db8::1", .port = 53, .scope_id = "en0"})(), forge::exceptions::base,
                         [](const auto& error) { return has_code(error, dns::exceptions::code::invalid_options); });
   BOOST_CHECK_EXCEPTION(rejects({.address = "127.0.0.1", .port = 53, .scope_id = "lo0"})(), forge::exceptions::base,
                         [](const auto& error) { return has_code(error, dns::exceptions::code::invalid_options); });
   BOOST_CHECK_EXCEPTION(rejects({.address = "fe80::1", .port = 53, .scope_id = "en 0"})(), forge::exceptions::base,
                         [](const auto& error) { return has_code(error, dns::exceptions::code::invalid_options); });
}

BOOST_AUTO_TEST_CASE(external_stop_racing_operation_retire_is_repeatable) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server, 1);

   for (auto attempt = 0; attempt < 16; ++attempt) {
      auto source = std::make_shared<std::stop_source>();
      auto query_done = std::make_shared<std::atomic_bool>(false);
      asio::co_spawn(
          runtime.context(), [&resolver, source, query_done]() -> asio::awaitable<void> {
             try {
                static_cast<void>(co_await resolver.async_resolve_addresses("stall.test", dns::address_family::ipv4,
                                                                              {}, source->get_token()));
             } catch (...) {
             }
             query_done->store(true, std::memory_order_release);
          },
          asio::detached);
      BOOST_REQUIRE(forge::asio::blocking::run_for(runtime, wait_for_active_queries(resolver), std::chrono::seconds{1}));

      auto release_stop = std::atomic_bool{false};
      auto external_stop = std::thread{[source, &release_stop] {
         while (!release_stop.load(std::memory_order_acquire)) {
            std::this_thread::yield();
         }
         source->request_stop();
      }};
      release_stop.store(true, std::memory_order_release);
      resolver.request_cancel();
      external_stop.join();

      BOOST_REQUIRE(forge::asio::blocking::run_for(runtime, wait_for_flag(*query_done), std::chrono::seconds{1}));
      BOOST_REQUIRE(forge::asio::blocking::run_for(runtime, wait_for_no_active_queries(resolver), std::chrono::seconds{1}));
      const auto recovered = forge::asio::blocking::run(
          runtime, resolver.async_resolve_addresses("a.test", dns::address_family::ipv4));
      BOOST_REQUIRE_EQUAL(recovered.answers.size(), 1U);
      BOOST_REQUIRE(forge::asio::blocking::run_for(runtime, wait_for_no_active_queries(resolver), std::chrono::seconds{1}));
   }

   forge::asio::blocking::run(runtime, resolver.async_close());
}

BOOST_AUTO_TEST_CASE(canceled_async_close_waiter_still_drains_stalled_operation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server);
   auto query_done = std::make_shared<std::atomic_bool>(false);
   asio::co_spawn(
       runtime.context(), [&resolver, query_done]() -> asio::awaitable<void> {
          try {
             static_cast<void>(co_await resolver.async_resolve_addresses("stall.test", dns::address_family::ipv4));
          } catch (...) {
          }
          query_done->store(true, std::memory_order_release);
       },
       asio::detached);
   BOOST_REQUIRE(forge::asio::blocking::run_for(runtime, wait_for_active_queries(resolver), std::chrono::seconds{1}));

   auto cancellation = std::make_shared<asio::cancellation_signal>();
   auto closed = asio::co_spawn(runtime.context(), resolver.async_close(),
                                 asio::bind_cancellation_slot(cancellation->slot(), asio::use_future));
   asio::post(runtime.context(), [cancellation] { cancellation->emit(asio::cancellation_type::terminal); });
   BOOST_REQUIRE(closed.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   BOOST_CHECK_NO_THROW(static_cast<void>(closed.get()));
   BOOST_CHECK_EQUAL(resolver.active_queries(), 0U);
   BOOST_REQUIRE(forge::asio::blocking::run_for(runtime, wait_for_flag(*query_done), std::chrono::seconds{1}));
}

BOOST_AUTO_TEST_CASE(maps_conflicting_cname_chain_to_codec_error) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};
   auto resolver = make_resolver(runtime, server);

   BOOST_CHECK_EXCEPTION(
       forge::asio::blocking::run(runtime,
                                  resolver.async_resolve_addresses("cname-conflict.test", dns::address_family::ipv4)),
       forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::codec_error); });
   forge::asio::blocking::run(runtime, resolver.async_close());
}

BOOST_AUTO_TEST_CASE(cancellation_deadline_and_close_have_distinct_terminal_errors) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};

   {
      auto resolver = make_resolver(runtime, server);
      auto source = std::stop_source{};
      source.request_stop();
      BOOST_CHECK_EXCEPTION(
          forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("a.test", dns::address_family::ipv4,
                                                                                  {}, source.get_token())),
          forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::canceled); });
      forge::asio::blocking::run(runtime, resolver.async_close());
   }

   {
      auto resolver = make_resolver(runtime, server);
      auto source = std::make_shared<std::stop_source>();
      auto timer = std::make_shared<asio::steady_timer>(runtime.context());
      timer->expires_after(std::chrono::milliseconds{5});
      asio::co_spawn(
          runtime.context(), [timer, source]() -> asio::awaitable<void> {
             auto error = boost::system::error_code{};
             co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, error));
             if (!error) {
                source->request_stop();
             }
          },
          asio::detached);
      BOOST_CHECK_EXCEPTION(
          forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("stall.test", dns::address_family::ipv4,
                                                                                  {}, source->get_token())),
          forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::canceled); });
      forge::asio::blocking::run(runtime, resolver.async_close());
   }

   {
      auto resolver = make_resolver(runtime, server);
      auto timer = std::make_shared<asio::steady_timer>(runtime.context());
      timer->expires_after(std::chrono::milliseconds{5});
      asio::co_spawn(
          runtime.context(), [timer, &resolver]() -> asio::awaitable<void> {
             auto error = boost::system::error_code{};
             co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, error));
             if (!error) {
                resolver.request_cancel();
             }
          },
          asio::detached);
      BOOST_CHECK_EXCEPTION(
          forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("stall.test", dns::address_family::ipv4)),
          forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::canceled); });
      forge::asio::blocking::run(runtime, resolver.async_close());
   }

   {
      auto resolver = make_resolver(runtime, server);
      auto options = dns::query_options{};
      options.deadline = std::chrono::steady_clock::now();
      BOOST_CHECK_EXCEPTION(
          forge::asio::blocking::run(runtime, resolver.async_resolve_addresses("stall.test", dns::address_family::ipv4,
                                                                                  options)),
          forge::exceptions::base, [](const auto& error) { return has_code(error, dns::exceptions::code::timeout); });
      forge::asio::blocking::run(runtime, resolver.async_close());
   }

   {
      auto resolver = make_resolver(runtime, server);
      auto error = std::make_shared<std::exception_ptr>();
      auto done = std::make_shared<std::atomic_bool>(false);
      asio::co_spawn(
          runtime.context(), [&resolver, error, done]() -> asio::awaitable<void> {
             try {
                static_cast<void>(co_await resolver.async_resolve_addresses("stall.test", dns::address_family::ipv4));
             } catch (...) {
                *error = std::current_exception();
             }
             done->store(true, std::memory_order_release);
          },
          asio::detached);
      forge::asio::blocking::run(runtime, wait_for_active_queries(resolver));
      forge::asio::blocking::run(runtime, resolver.async_close());
      BOOST_REQUIRE(done->load(std::memory_order_acquire));
      BOOST_REQUIRE(*error);
      try {
         std::rethrow_exception(*error);
      } catch (const forge::exceptions::base& value) {
         BOOST_CHECK(has_code(value, dns::exceptions::code::closed));
      }
   }
}

BOOST_AUTO_TEST_CASE(owns_resolver_state_across_move_and_destruction_before_await) {
   auto runtime = forge::asio::runtime{};
   auto server = local_dns_server{response_for};

   auto first = make_resolver(runtime, server);
   auto resolver = dns::resolver{std::move(first)};
   const auto recovered = forge::asio::blocking::run(
       runtime, resolver.async_resolve_addresses("a.test", dns::address_family::ipv4));
   BOOST_CHECK_EQUAL(recovered.answers.size(), 1U);
   forge::asio::blocking::run(runtime, resolver.async_close());

   auto pending = std::optional<boost::asio::awaitable<dns::address_response>>{};
   {
      auto transient = std::make_unique<dns::resolver>(make_resolver(runtime, server));
      pending.emplace(transient->async_resolve_addresses("stall.test", dns::address_family::ipv4));
   }
   BOOST_CHECK_EXCEPTION(forge::asio::blocking::run(runtime, std::move(*pending)), forge::exceptions::base,
                         [](const auto& error) { return has_code(error, dns::exceptions::code::closed); });
}

} // namespace
