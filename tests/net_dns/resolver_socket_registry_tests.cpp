module;

#if defined(_WIN32)
#include <winsock2.h>
#endif

#include <ares.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/system/error_code.hpp>

#if defined(_WIN32)
#include <boost/asio/generic/datagram_protocol.hpp>
#include <boost/asio/generic/stream_protocol.hpp>
#else
#include <boost/asio/posix/stream_descriptor.hpp>
#endif

module forge.net.dns.resolver;

#include "details/resolver_socket_registry.hxx"
#include "details/resolver_socket_watch.hxx"

namespace {

namespace asio = boost::asio;
namespace dns = forge::net::dns;
using udp = asio::ip::udp;

BOOST_AUTO_TEST_CASE(socket_registry_rejects_stale_watch_after_same_fd_replacement) {
   auto context = asio::io_context{};
   auto socket = udp::socket{context, udp::endpoint{asio::ip::address_v4::loopback(), 0}};
   const auto fd = static_cast<ares_socket_t>(socket.native_handle());
   auto registry = dns::resolver_socket_registry{context.get_executor()};

   const auto old_watch = registry.create_or_find(fd);
   const auto old_generation = old_watch->generation();
   BOOST_REQUIRE(registry.find(fd) == old_watch);
   BOOST_REQUIRE(registry.contains(fd, old_watch));
   BOOST_REQUIRE_EQUAL(registry.snapshot().size(), 1U);

   const auto removed = registry.remove(fd);
   BOOST_REQUIRE(removed == old_watch);
   BOOST_CHECK(socket.is_open());
   BOOST_CHECK(!registry.find(fd));
   BOOST_CHECK(!registry.contains(fd, old_watch));

   const auto replacement = registry.create_or_find(fd);
   BOOST_CHECK(old_watch != replacement);
   BOOST_CHECK_NE(replacement->generation(), old_generation);
   BOOST_CHECK(!registry.contains(fd, old_watch));
   BOOST_CHECK(registry.contains(fd, replacement));
   BOOST_REQUIRE_EQUAL(registry.snapshot().size(), 1U);
   BOOST_CHECK(registry.snapshot().front() == replacement);

   registry.release_all();
   BOOST_CHECK(socket.is_open());
}

} // namespace
