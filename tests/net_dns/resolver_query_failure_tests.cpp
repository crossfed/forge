module;

#include <boost/test/unit_test.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <new>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/use_future.hpp>

#include "../fixtures/local_dns_server.hxx"

module forge.net.dns.resolver;

#include "details/resolver_wait_failure.hxx"

namespace {

namespace asio = boost::asio;
namespace dns = forge::net::dns;
using udp = asio::ip::udp;
using namespace std::chrono_literals;

template <typename T>
bool drive_ready(asio::io_context& context, std::future<T>& future) {
   const auto deadline = std::chrono::steady_clock::now() + 500ms;
   while (future.wait_for(0ms) != std::future_status::ready && std::chrono::steady_clock::now() < deadline) {
      context.restart();
      context.run_for(1ms);
   }
   return future.wait_for(0ms) == std::future_status::ready;
}

void check_drained(asio::io_context& context, dns::resolver& resolver) {
   auto close = asio::co_spawn(context, resolver.async_close(), asio::use_future);
   BOOST_REQUIRE(drive_ready(context, close));
   BOOST_CHECK_NO_THROW(close.get());
   BOOST_CHECK_EQUAL(resolver.active_queries(), 0U);
}

BOOST_AUTO_TEST_CASE(dns_read_wait_initiation_failure_reports_error_and_drains) {
   auto context = asio::io_context{};
   auto server = udp::socket{context, udp::endpoint{asio::ip::address_v4::loopback(), 0}};
   auto resolver = dns::resolver{context.get_executor(),
       {.nameservers = {{.address = "127.0.0.1", .port = server.local_endpoint().port()}}}};
   auto failure = dns::resolver_wait_failure{dns::resolver_wait_failure::point::read};
   auto query = asio::co_spawn(context, resolver.async_resolve_addresses("wait-failure.test", dns::address_family::ipv4),
                               asio::use_future);
   BOOST_REQUIRE(drive_ready(context, query));
   BOOST_REQUIRE(failure.triggered());
   BOOST_CHECK_THROW(static_cast<void>(query.get()), std::bad_alloc);
   check_drained(context, resolver);
}

BOOST_AUTO_TEST_CASE(dns_socket_ready_timer_rearm_failure_reports_error_and_drains) {
   auto context = asio::io_context{};
   auto server = udp::socket{context, udp::endpoint{asio::ip::address_v4::loopback(), 0}};
   server.non_blocking(true);
   auto resolver = dns::resolver{context.get_executor(),
       {.nameservers = {{.address = "127.0.0.1", .port = server.local_endpoint().port()}}}};
   auto query = asio::co_spawn(context, resolver.async_resolve_addresses("timer-failure.test"), asio::use_future);
   auto response = std::vector<std::uint8_t>{};
   auto recipient = udp::endpoint{};
   auto received = 0U;
   const auto deadline = std::chrono::steady_clock::now() + 500ms;
   while (received != 2 && std::chrono::steady_clock::now() < deadline) {
      context.restart();
      context.run_for(1ms);
      auto bytes = std::array<std::uint8_t, 4096>{};
      auto remote = udp::endpoint{};
      auto error = boost::system::error_code{};
      const auto size = server.receive_from(asio::buffer(bytes), remote, 0, error);
      if (error == asio::error::would_block || error == asio::error::try_again) {
         continue;
      }
      BOOST_REQUIRE(!error);
      const auto question = forge::tests::dns::parse_question(bytes.data(), size);
      BOOST_REQUIRE(question);
      ++received;
      if (question->type == 1) {
         recipient = remote;
         response = forge::tests::dns::make_response(bytes.data(), *question,
             {{.type = 1, .value = {192, 0, 2, 7}}});
      }
   }
   BOOST_REQUIRE_EQUAL(received, 2U);
   BOOST_REQUIRE(!response.empty());
   BOOST_REQUIRE(query.wait_for(0ms) != std::future_status::ready);
   // Leave AAAA pending so the actual A readiness callback rearms c-ares' timer.
   auto failure = dns::resolver_wait_failure{dns::resolver_wait_failure::point::timer};
   server.send_to(asio::buffer(response), recipient);
   BOOST_REQUIRE(drive_ready(context, query));
   BOOST_REQUIRE(failure.triggered());
   BOOST_CHECK_THROW(static_cast<void>(query.get()), std::bad_alloc);
   check_drained(context, resolver);
}

} // namespace
