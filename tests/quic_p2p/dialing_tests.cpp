module;

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <initializer_list>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.net.p2p.dialing;
import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.multiformats.multiaddr;

#include "../../libraries/net/p2p/details/black_hole_detector.hxx"
#include "../../libraries/net/p2p/details/dial_ranker.hxx"
#include "../../libraries/net/p2p/details/host_addresses.hxx"

namespace forge::net::p2p {
namespace {

[[nodiscard]] endpoint endpoint_from(std::string_view value) {
   return parse_endpoint(value);
}

[[nodiscard]] std::vector<detail::dial_plan_item> rank(std::initializer_list<std::string_view> values,
                                                        dialing::ranker_policy policy = {}) {
   auto endpoints = std::vector<endpoint>{};
   endpoints.reserve(values.size());
   for (const auto value : values) {
      endpoints.push_back(endpoint_from(value));
   }
   return detail::dial_ranker{policy}.rank(std::move(endpoints));
}

void check_plan_item(const detail::dial_plan_item& value, std::string_view expected, std::int64_t delay) {
   BOOST_TEST(value.value.to_string() == expected);
   BOOST_TEST(value.delay.count() == delay);
}

void record(detail::black_hole_detector& value, const endpoint& address, dialing::outcome outcome,
            std::size_t count) {
   for (auto index = std::size_t{0}; index < count; ++index) {
      value.record_address_outcome(address, outcome);
   }
}

void check_scope(std::string_view value, host_addresses::endpoint_scope expected) {
   BOOST_TEST(static_cast<int>(host_addresses::classify_endpoint_scope(endpoint_from(value))) ==
              static_cast<int>(expected));
}

} // namespace

BOOST_AUTO_TEST_SUITE(dialing_tests)

BOOST_AUTO_TEST_CASE(host_address_scope_matches_go_multiaddr_donor_vectors) {
   check_scope("/ip4/1.2.3.4/tcp/1", host_addresses::endpoint_scope::public_address);
   check_scope("/ip4/8.8.8.8/tcp/1", host_addresses::endpoint_scope::public_address);
   check_scope("/ip4/10.0.0.1/tcp/1", host_addresses::endpoint_scope::private_address);
   check_scope("/ip4/100.64.0.1/tcp/1", host_addresses::endpoint_scope::private_address);
   check_scope("/ip4/172.16.0.1/tcp/1", host_addresses::endpoint_scope::private_address);
   check_scope("/ip4/192.168.0.1/tcp/1", host_addresses::endpoint_scope::private_address);
   check_scope("/ip4/127.0.0.1/tcp/1", host_addresses::endpoint_scope::loopback);
   check_scope("/ip4/169.254.0.1/tcp/1", host_addresses::endpoint_scope::link_local);
   check_scope("/ip4/0.0.0.0/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip4/192.0.0.1/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip4/192.0.2.1/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip4/192.88.99.1/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip4/198.18.0.1/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip4/198.51.100.1/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip4/203.0.113.1/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip4/224.0.0.1/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip4/240.0.0.1/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip4/255.255.255.255/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip6/2400::1/tcp/1", host_addresses::endpoint_scope::public_address);
   check_scope("/ip6/64:ff9b::192.0.2.1/tcp/1", host_addresses::endpoint_scope::public_address);
   check_scope("/ip6/64:ff9b:1::1/tcp/1", host_addresses::endpoint_scope::public_address);
   check_scope("/ip6/2001:db8::1/tcp/1", host_addresses::endpoint_scope::unroutable);
   check_scope("/ip6/::1/tcp/1", host_addresses::endpoint_scope::loopback);
   check_scope("/ip6/fe80::1/tcp/1", host_addresses::endpoint_scope::link_local);
   check_scope("/ip6/fd00::1/tcp/1", host_addresses::endpoint_scope::private_address);
   check_scope("/ip6/::/tcp/1", host_addresses::endpoint_scope::unroutable);
}

BOOST_AUTO_TEST_CASE(host_addresses_preserve_dnsaddr_carriers_and_canonical_peer_suffixes) {
   const auto target = peer_id::from_string("QmcgpsyWgH8Y8ajJz1Cu72KnS5uo2Aa2LpzU7kinSupNKC");
   const auto relay = peer_id::from_string("QmNLfbof5rLekrACjeuLk9JmGZD2HDBHCU4z16iYKmx5SE");
   const auto context = host_addresses::learning_context{.source = host_addresses::source_kind::third_party};

   const auto nonleading_dnsaddr = forge::multiformats::multiaddr::parse("/ip4/8.8.8.8/dnsaddr/relay.example");
   const auto learned_dnsaddr = host_addresses::learned(nonleading_dnsaddr, target, context);
   BOOST_REQUIRE(learned_dnsaddr.has_value());
   BOOST_TEST(learned_dnsaddr->to_string() == nonleading_dnsaddr.to_string() + "/p2p/" + target.to_string());

   const auto relay_carrier = forge::multiformats::multiaddr::parse(
       "/dnsaddr/relay.example/p2p/" + relay.to_string() + "/p2p-circuit/p2p/" + target.to_string());
   const auto learned_relay = host_addresses::learned(relay_carrier, target, context);
   BOOST_REQUIRE(learned_relay.has_value());
   BOOST_TEST(learned_relay->to_string() == relay_carrier.to_string());

   const auto anonymous_relay_carrier = forge::multiformats::multiaddr::parse(
       "/dnsaddr/relay.example/p2p-circuit/p2p/" + target.to_string());
   const auto learned_anonymous_relay = host_addresses::learned(anonymous_relay_carrier, target, context);
   BOOST_REQUIRE(learned_anonymous_relay.has_value());
   BOOST_TEST(learned_anonymous_relay->to_string() == anonymous_relay_carrier.to_string());

   const auto terminal = forge::multiformats::multiaddr::parse("/dnsaddr/target.example/p2p/" + target.to_string());
   const auto learned_terminal = host_addresses::learned(terminal, target, context);
   BOOST_REQUIRE(learned_terminal.has_value());
   BOOST_TEST(learned_terminal->to_string() == terminal.to_string());

   const auto malformed_relay = forge::multiformats::multiaddr::parse(
       "/dnsaddr/relay.example/p2p/1/p2p-circuit/p2p/" + target.to_string());
   BOOST_TEST(!host_addresses::learned(malformed_relay, target, context).has_value());

   const auto unrelated_after_circuit = forge::multiformats::multiaddr::parse(
       "/dnsaddr/relay.example/p2p-circuit/tcp/4001/p2p/" + target.to_string());
   BOOST_TEST(!host_addresses::learned(unrelated_after_circuit, target, context).has_value());

   const auto nonterminal_target = forge::multiformats::multiaddr::parse(
       "/dnsaddr/target.example/p2p/" + target.to_string() + "/tcp/4001");
   BOOST_TEST(!host_addresses::learned(nonterminal_target, target, context).has_value());

   const auto conflicting_terminal =
       forge::multiformats::multiaddr::parse("/dnsaddr/target.example/p2p/" + relay.to_string());
   BOOST_TEST(!host_addresses::learned(conflicting_terminal, target, context).has_value());
}

BOOST_AUTO_TEST_CASE(dial_ranker_matches_direct_quic_happy_eyeballs) {
   const auto values = rank({"/ip6/2400::2/udp/4002/quic-v1", "/ip6/2400::2/udp/4001/quic-v1",
                             "/ip4/1.2.3.4/udp/1/quic-v1"});
   BOOST_REQUIRE_EQUAL(values.size(), 3U);
   check_plan_item(values[0], "/ip6/2400::2/udp/4001/quic-v1", 0);
   check_plan_item(values[1], "/ip4/1.2.3.4/udp/1/quic-v1", 250);
   check_plan_item(values[2], "/ip6/2400::2/udp/4002/quic-v1", 500);
   BOOST_CHECK(!values[0].tcp);
   BOOST_TEST(values[0].tcp_handshake_progress_hold.count() == 0);
}

BOOST_AUTO_TEST_CASE(dial_ranker_matches_direct_tcp_happy_eyeballs) {
   const auto values = rank({"/ip6/2400::2/tcp/4002", "/ip6/2400::2/tcp/4001", "/ip4/1.2.3.4/tcp/1",
                             "/ip4/1.2.3.4/tcp/4002"});
   BOOST_REQUIRE_EQUAL(values.size(), 4U);
   check_plan_item(values[0], "/ip6/2400::2/tcp/4001", 0);
   check_plan_item(values[1], "/ip4/1.2.3.4/tcp/1", 250);
   check_plan_item(values[2], "/ip6/2400::2/tcp/4002", 500);
   check_plan_item(values[3], "/ip4/1.2.3.4/tcp/4002", 500);
   BOOST_CHECK(values[0].tcp);
   BOOST_TEST(values[0].tcp_handshake_progress_hold.count() == 250);
}

BOOST_AUTO_TEST_CASE(dial_ranker_prefers_quic_then_tcp_and_preserves_progress_hold_metadata) {
   const auto values = rank({"/ip6/2400::2/udp/4001/quic-v1", "/ip4/1.2.3.4/udp/1/quic-v1",
                             "/ip6/2400::2/tcp/4001", "/ip4/8.8.8.8/tcp/1", "/ip4/8.8.8.8/tcp/2"});
   BOOST_REQUIRE_EQUAL(values.size(), 5U);
   check_plan_item(values[0], "/ip6/2400::2/udp/4001/quic-v1", 0);
   check_plan_item(values[1], "/ip4/1.2.3.4/udp/1/quic-v1", 250);
   check_plan_item(values[2], "/ip6/2400::2/tcp/4001", 500);
   check_plan_item(values[3], "/ip4/8.8.8.8/tcp/1", 750);
   check_plan_item(values[4], "/ip4/8.8.8.8/tcp/2", 1000);
   BOOST_TEST(values[2].tcp_handshake_progress_hold.count() == 250);
}

BOOST_AUTO_TEST_CASE(dial_ranker_starts_private_and_public_groups_in_parallel) {
   const auto values = rank({"/ip4/192.168.1.1/tcp/4001", "/ip4/8.8.8.8/tcp/4001"});
   BOOST_REQUIRE_EQUAL(values.size(), 2U);
   check_plan_item(values[0], "/ip4/192.168.1.1/tcp/4001", 0);
   check_plan_item(values[1], "/ip4/8.8.8.8/tcp/4001", 0);
   BOOST_TEST(values[0].tcp_handshake_progress_hold.count() == 0);
   BOOST_TEST(values[1].tcp_handshake_progress_hold.count() == 250);

   const auto private_values = rank({"/ip6/fd00::1/udp/4001/quic-v1", "/ip4/192.168.1.1/udp/1/quic-v1"});
   BOOST_REQUIRE_EQUAL(private_values.size(), 2U);
   BOOST_TEST(private_values[0].delay.count() == 0);
   BOOST_TEST(private_values[1].delay.count() == 30);
}

BOOST_AUTO_TEST_CASE(dial_ranker_uses_lowest_port_then_endpoint_string) {
   const auto values = rank({"/ip4/8.8.8.2/tcp/4002", "/ip4/8.8.8.10/tcp/4001", "/ip4/8.8.8.1/tcp/4001"});
   BOOST_REQUIRE_EQUAL(values.size(), 3U);
   check_plan_item(values[0], "/ip4/8.8.8.1/tcp/4001", 0);
   check_plan_item(values[1], "/ip4/8.8.8.10/tcp/4001", 250);
   check_plan_item(values[2], "/ip4/8.8.8.2/tcp/4002", 250);
}

BOOST_AUTO_TEST_CASE(dialing_policies_only_allow_bounded_hardening) {
   BOOST_CHECK_NO_THROW((void)detail::dial_ranker{});
   BOOST_CHECK_THROW((void)detail::dial_ranker{dialing::ranker_policy{.public_delay = std::chrono::milliseconds{251}}},
                     exceptions::invalid_options);
   BOOST_CHECK_THROW(
       (void)detail::dial_ranker{dialing::ranker_policy{.private_delay = std::chrono::milliseconds::zero()}},
                     exceptions::invalid_options);

   BOOST_CHECK_NO_THROW((void)detail::black_hole_detector{});
   const auto tightened = dialing::black_hole_policy{.window_size = 4, .min_successes = 1};
   const auto over_window = dialing::black_hole_policy{.window_size = 101, .min_successes = 5};
   const auto weakened = dialing::black_hole_policy{.window_size = 100, .min_successes = 4};
   const auto min_over_window = dialing::black_hole_policy{.window_size = 4, .min_successes = 5};
   const auto zero_window = dialing::black_hole_policy{.window_size = 0, .min_successes = 1};
   BOOST_CHECK_NO_THROW((void)detail::black_hole_detector{tightened});
   BOOST_CHECK_THROW((void)detail::black_hole_detector{over_window}, exceptions::invalid_options);
   BOOST_CHECK_THROW((void)detail::black_hole_detector{weakened}, exceptions::invalid_options);
   BOOST_CHECK_THROW((void)detail::black_hole_detector{min_over_window}, exceptions::invalid_options);
   BOOST_CHECK_THROW((void)detail::black_hole_detector{zero_window}, exceptions::invalid_options);
}

BOOST_AUTO_TEST_CASE(black_hole_detector_transitions_after_a_full_bounded_window) {
   const auto public_udp = endpoint_from("/ip4/8.8.8.8/udp/4001/quic-v1");

   auto probing = detail::black_hole_detector{};
   record(probing, public_udp, dialing::outcome::failure, 99);
   const auto probing_status = probing.status().udp;
   BOOST_TEST(static_cast<int>(probing_status.state) == static_cast<int>(dialing::black_hole_state::probing));
   BOOST_TEST(probing_status.outcomes == 99U);

   auto allowed = detail::black_hole_detector{};
   record(allowed, public_udp, dialing::outcome::failure, 95);
   record(allowed, public_udp, dialing::outcome::success, 5);
   const auto allowed_status = allowed.status().udp;
   BOOST_TEST(static_cast<int>(allowed_status.state) == static_cast<int>(dialing::black_hole_state::allowed));
   BOOST_TEST(allowed_status.successes == 5U);

   auto blocked = detail::black_hole_detector{};
   record(blocked, public_udp, dialing::outcome::failure, 96);
   record(blocked, public_udp, dialing::outcome::success, 4);
   const auto blocked_status = blocked.status().udp;
   BOOST_TEST(static_cast<int>(blocked_status.state) == static_cast<int>(dialing::black_hole_state::blocked));
   BOOST_TEST(blocked_status.successes == 4U);
}

BOOST_AUTO_TEST_CASE(black_hole_detector_probes_and_resets_per_logical_peer_dial) {
   const auto public_udp = endpoint_from("/ip4/8.8.8.8/udp/4001/quic-v1");
   const auto policy = dialing::black_hole_policy{.window_size = 4, .min_successes = 1};
   auto detector = detail::black_hole_detector{policy};
   record(detector, public_udp, dialing::outcome::failure, 4);

   for (auto request = std::size_t{1}; request < 4; ++request) {
      const auto filtered = detector.filter_peer_dial({public_udp, public_udp});
      BOOST_TEST(filtered.allowed.empty());
      BOOST_REQUIRE_EQUAL(filtered.blocked.size(), 2U);
      BOOST_TEST(detector.status().udp.peer_requests == request);
   }
   const auto probe = detector.filter_peer_dial({public_udp, public_udp});
   BOOST_REQUIRE_EQUAL(probe.allowed.size(), 2U);
   BOOST_TEST(probe.blocked.empty());
   BOOST_TEST(detector.status().udp.peer_requests == 4U);

   detector.record_address_outcome(public_udp, dialing::outcome::success);
   const auto reset = detector.status().udp;
   BOOST_TEST(static_cast<int>(reset.state) == static_cast<int>(dialing::black_hole_state::probing));
   BOOST_TEST(reset.peer_requests == 0U);
   BOOST_TEST(reset.outcomes == 0U);
}

BOOST_AUTO_TEST_CASE(black_hole_detector_counts_requests_and_address_outcomes_separately) {
   const auto public_udp4 = endpoint_from("/ip4/8.8.8.8/udp/4001/quic-v1");
   const auto public_tcp6 = endpoint_from("/ip6/2400::1/tcp/4001");
   const auto public_udp6 = endpoint_from("/ip6/2400::1/udp/4002/quic-v1");
   auto detector = detail::black_hole_detector{};

   const auto filtered = detector.filter_peer_dial({public_udp4, public_tcp6, public_udp6});
   BOOST_REQUIRE_EQUAL(filtered.allowed.size(), 3U);
   BOOST_TEST(detector.status().udp.peer_requests == 1U);
   BOOST_TEST(detector.status().ipv6.peer_requests == 1U);

   detector.record_address_outcome(public_udp6, dialing::outcome::neutral);
   BOOST_TEST(detector.status().udp.outcomes == 0U);
   BOOST_TEST(detector.status().ipv6.outcomes == 0U);
   detector.record_address_outcome(public_udp6, dialing::outcome::failure);
   BOOST_TEST(detector.status().udp.outcomes == 1U);
   BOOST_TEST(detector.status().ipv6.outcomes == 1U);
}

BOOST_AUTO_TEST_CASE(black_hole_detector_ignores_nonpublic_addresses_and_can_disable_each_counter) {
   const auto loopback = endpoint_from("/ip4/127.0.0.1/udp/4001/quic-v1");
   const auto private_address = endpoint_from("/ip4/192.168.1.1/udp/4001/quic-v1");
   const auto link_local = endpoint_from("/ip6/fe80::1/udp/4001/quic-v1");
   auto detector = detail::black_hole_detector{};
   const auto filtered = detector.filter_peer_dial({loopback, private_address, link_local});
   BOOST_REQUIRE_EQUAL(filtered.allowed.size(), 3U);
   detector.record_address_outcome(loopback, dialing::outcome::failure);
   detector.record_address_outcome(private_address, dialing::outcome::failure);
   detector.record_address_outcome(link_local, dialing::outcome::failure);
   BOOST_TEST(detector.status().udp.peer_requests == 0U);
   BOOST_TEST(detector.status().ipv6.peer_requests == 0U);
   BOOST_TEST(detector.status().udp.outcomes == 0U);
   BOOST_TEST(detector.status().ipv6.outcomes == 0U);

   const auto public_udp = endpoint_from("/ip4/8.8.8.8/udp/4001/quic-v1");
   const auto ipv6_only_policy = dialing::black_hole_policy{.udp_enabled = false, .ipv6_enabled = true};
   auto ipv6_only = detail::black_hole_detector{ipv6_only_policy};
   record(ipv6_only, public_udp, dialing::outcome::failure, 100);
   const auto status = ipv6_only.status();
   BOOST_TEST(!status.udp.enabled);
   BOOST_TEST(status.udp.outcomes == 0U);
}

BOOST_AUTO_TEST_CASE(black_hole_detector_gives_each_probing_dimension_precedence) {
   const auto public_udp4 = endpoint_from("/ip4/8.8.8.8/udp/4001/quic-v1");
   const auto public_tcp6 = endpoint_from("/ip6/2400::1/tcp/4001");
   const auto public_udp6 = endpoint_from("/ip6/2400::1/udp/4001/quic-v1");
   const auto policy = dialing::black_hole_policy{.window_size = 1, .min_successes = 1};

   auto udp_blocked = detail::black_hole_detector{policy};
   udp_blocked.record_address_outcome(public_udp4, dialing::outcome::failure);
   const auto udp_probe = udp_blocked.filter_peer_dial({public_udp6});
   BOOST_REQUIRE_EQUAL(udp_probe.allowed.size(), 1U);

   auto ipv6_blocked = detail::black_hole_detector{policy};
   ipv6_blocked.record_address_outcome(public_tcp6, dialing::outcome::failure);
   const auto ipv6_probe = ipv6_blocked.filter_peer_dial({public_udp6});
   BOOST_REQUIRE_EQUAL(ipv6_probe.allowed.size(), 1U);
}

BOOST_AUTO_TEST_CASE(black_hole_detector_serializes_concurrent_requests_outcomes_and_status) {
   const auto public_udp = endpoint_from("/ip4/8.8.8.8/udp/4001/quic-v1");
   auto detector = detail::black_hole_detector{};
   auto valid = std::atomic_bool{true};
   constexpr auto workers = std::size_t{4};
   constexpr auto iterations = std::size_t{32};
   auto threads = std::vector<std::thread>{};
   threads.reserve(workers);
   for (auto worker = std::size_t{0}; worker < workers; ++worker) {
      threads.emplace_back([&] {
         for (auto iteration = std::size_t{0}; iteration < iterations; ++iteration) {
            static_cast<void>(detector.filter_peer_dial({public_udp, public_udp}));
            detector.record_address_outcome(public_udp, dialing::outcome::failure);
            const auto current = detector.status().udp;
            const auto valid_state = static_cast<unsigned>(current.state) <=
                                     static_cast<unsigned>(dialing::black_hole_state::blocked);
            if (!valid_state || current.outcomes > 100 || current.successes > current.outcomes ||
                current.next_probe_after > 100) {
               valid.store(false);
            }
         }
      });
   }
   for (auto& worker : threads) {
      worker.join();
   }

   const auto status = detector.status().udp;
   BOOST_TEST(valid.load());
   BOOST_TEST(status.peer_requests == workers * iterations);
   BOOST_TEST(status.outcomes == 100U);
   BOOST_TEST(status.successes == 0U);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace forge::net::p2p
