module;

#include <boost/test/unit_test.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.exceptions;
import forge.net.p2p.dht;
import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;

#include "../../libraries/net/p2p/details/dht_query.hxx"

namespace forge::net::p2p {
namespace {

[[nodiscard]] peer_id query_test_peer(std::uint8_t seed) {
   return make_peer_id(public_key{.type = public_key::type::ed25519, .data = std::vector<std::uint8_t>(32, seed)});
}

[[nodiscard]] endpoint query_test_endpoint(std::uint16_t port) {
   return parse_endpoint("/ip4/127.0.0.1/tcp/" + std::to_string(port));
}

} // namespace

BOOST_AUTO_TEST_SUITE(dht_query_tests)

BOOST_AUTO_TEST_CASE(dht_query_merge_bounds_endpoints_across_repeated_peer_records) {
   const auto id = query_test_peer(1);
   const auto first = dht::peer{
       .id = id,
       .endpoints = {query_test_endpoint(4101), query_test_endpoint(4102)},
   };
   const auto second = dht::peer{
       .id = id,
       .endpoints = {query_test_endpoint(4102), query_test_endpoint(4103), query_test_endpoint(4104)},
   };

   auto known = std::map<peer_id, dht::peer>{};
   const auto target = make_dht_key(id);
   dht_query::merge_known(known, first, 20, 3, target);
   dht_query::merge_known(known, second, 20, 3, target);
   BOOST_REQUIRE_EQUAL(known.size(), 1U);
   BOOST_REQUIRE_EQUAL(known.begin()->second.endpoints.size(), 3U);
   BOOST_CHECK(known.begin()->second.endpoints[2].to_string() == query_test_endpoint(4103).to_string());

   auto providers = std::vector<dht::peer>{};
   dht_query::merge_provider(providers, first, 20, 3);
   dht_query::merge_provider(providers, second, 20, 3);
   BOOST_REQUIRE_EQUAL(providers.size(), 1U);
   BOOST_REQUIRE_EQUAL(providers.front().endpoints.size(), 3U);
   BOOST_CHECK(providers.front().endpoints[2].to_string() == query_test_endpoint(4103).to_string());
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace forge::net::p2p
