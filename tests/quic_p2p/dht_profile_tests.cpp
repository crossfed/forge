#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import forge.net.p2p.dht;

import forge.multiformats.multihash;
import forge.multiformats.types;
import forge.multiformats.varint;
import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;

namespace forge::net::p2p {
namespace {

[[nodiscard]] std::vector<std::uint8_t> bytes(std::string_view value) {
   return {value.begin(), value.end()};
}

[[nodiscard]] peer_id test_peer(std::uint8_t value) {
   const auto payload = forge::multiformats::bytes{value};
   return peer_id::from_bytes(forge::multiformats::multihash::identity(payload).encode());
}

[[nodiscard]] std::size_t framed_payload_size(std::span<const std::uint8_t> value) {
   return static_cast<std::size_t>(forge::multiformats::varint_decode(value).value);
}

[[nodiscard]] dht::profile custom_profile(dht::options limits = {}) {
   return custom_dht_profile(protocol_id{.value = "/forge/custom-kad/1.0.0"}, dht::mode::server,
                             dht::profile_capabilities{.peers = true, .providers = true, .values = false}, {}, limits);
}

} // namespace

BOOST_AUTO_TEST_SUITE(dht_profile_tests)

BOOST_AUTO_TEST_CASE(dht_amino_profile_is_fixed_and_complete) {
   const auto profile = amino_v1(dht::mode::server);

   BOOST_TEST(profile.protocol.value == builtins::kad_dht.value);
   BOOST_TEST(static_cast<int>(profile.kind) == static_cast<int>(dht::profile_kind::amino_v1));
   BOOST_TEST(static_cast<int>(profile.operating_mode) == static_cast<int>(dht::mode::server));
   BOOST_TEST(static_cast<int>(profile.limits.operating_mode) == static_cast<int>(dht::mode::server));
   BOOST_TEST(profile.capabilities.peers);
   BOOST_TEST(profile.capabilities.providers);
   BOOST_TEST(profile.capabilities.values);
   BOOST_TEST(profile.limits.replication == 20U);
   BOOST_TEST(profile.limits.alpha == 10U);
   BOOST_TEST(profile.limits.max_outbound_message_size == 16U * 1024U);
   BOOST_TEST(profile.limits.max_inbound_message_size == 4U * 1024U * 1024U);
   BOOST_TEST(profile.limits.max_record_size == 10U * 1024U);
   BOOST_TEST(profile.limits.max_closer_peers == 21U);
   BOOST_TEST(profile.limits.max_provider_peers == 256U);
   BOOST_TEST(profile.limits.max_peer_endpoints == 64U);
   BOOST_TEST(profile.limits.replacement_cache_size == 20U);
   BOOST_TEST(profile.limits.failure_threshold == 3U);
   BOOST_TEST(profile.limits.query_timeout == std::chrono::seconds{10});
   BOOST_TEST(profile.limits.refresh_interval == std::chrono::minutes{10});
   BOOST_TEST(profile.limits.provider_record_ttl == std::chrono::hours{48});
   BOOST_REQUIRE_EQUAL(profile.value_policies.size(), 2U);
   BOOST_TEST(profile.value_policies[0].key_prefix == bytes("/pk/"));
   BOOST_TEST(profile.value_policies[1].key_prefix == bytes("/ipns/"));

   auto changed = profile;
   changed.limits.failure_threshold = 4;
   BOOST_CHECK_THROW(validate(changed), exceptions::invalid_options);
   changed = profile;
   changed.limits.query_timeout = std::chrono::seconds{11};
   BOOST_CHECK_THROW(validate(changed), exceptions::invalid_options);
   changed = profile;
   changed.limits.refresh_interval = std::chrono::minutes{11};
   BOOST_CHECK_THROW(validate(changed), exceptions::invalid_options);
}

BOOST_AUTO_TEST_CASE(dht_amino_decoder_accepts_donor_peer_sets_beyond_outbound_k) {
   const auto amino = amino_v1();
   auto sender_limits = amino.limits;
   sender_limits.max_outbound_message_size = 1024 * 1024;
   auto message = dht::message{
       .type = dht::message_type::get_providers,
       .key_value = dht::key{.bytes = forge::multiformats::multihash::sha2_256(bytes("providers")).encode()},
   };
   for (auto value = std::uint8_t{1}; value <= 21; ++value) {
      message.closer_peers.push_back(dht::peer{.id = test_peer(value)});
   }
   for (auto value = std::uint8_t{22}; value <= 46; ++value) {
      message.provider_peers.push_back(dht::peer{.id = test_peer(value)});
   }

   const auto decoded = dht::codec::decode(dht::codec::encode(message, sender_limits), amino);
   BOOST_TEST(decoded.closer_peers.size() == 21U);
   BOOST_TEST(decoded.provider_peers.size() == 25U);
}

BOOST_AUTO_TEST_CASE(dht_custom_profile_keeps_protocol_limits_and_validators_isolated) {
   auto validated = false;
   auto selected = false;
   auto limits = dht::options{};
   limits.max_outbound_message_size = 32 * 1024;
   limits.max_inbound_message_size = 64 * 1024;
   const auto profile = custom_dht_profile(
       protocol_id{.value = "/forge/custom-values/1.0.0"}, dht::mode::server,
       dht::profile_capabilities{.peers = false, .providers = true, .values = true},
       {dht::value_policy{
           .key_prefix = bytes("/custom/"),
           .validate = [&](const dht::record& value,
                           dht::value_validation_context) { validated = value.value == bytes("value"); },
           .select =
               [&](std::span<const dht::record> candidates) {
                  selected = true;
                  return candidates.size() - 1;
               },
           .expiry = [](const dht::record&, dht::value_expiry_context context) { return context.supplied_expires_at; },
       }},
       limits);

   BOOST_TEST(static_cast<int>(profile.kind) == static_cast<int>(dht::profile_kind::custom));
   BOOST_TEST(profile.protocol.value == "/forge/custom-values/1.0.0");
   BOOST_TEST(profile.limits.max_outbound_message_size == 32U * 1024U);
   BOOST_TEST(profile.limits.max_inbound_message_size == 64U * 1024U);
   BOOST_TEST(static_cast<int>(profile.limits.operating_mode) == static_cast<int>(dht::mode::server));
   const auto key = bytes("/custom/key");
   const auto* policy = value_policy_for(profile, key);
   BOOST_REQUIRE(policy != nullptr);
   policy->validate(dht::record{.key_value = dht::key{.bytes = key}, .value = bytes("value")},
                    dht::value_validation_context{});
   const auto candidates = std::vector<dht::record>{
       dht::record{.key_value = dht::key{.bytes = key}, .value = bytes("old")},
       dht::record{.key_value = dht::key{.bytes = key}, .value = bytes("value")},
   };
   BOOST_TEST(policy->select(candidates) == 1U);
   BOOST_TEST(validated);
   BOOST_TEST(selected);
   BOOST_TEST(value_policy_for(amino_v1(), key) == nullptr);
   BOOST_CHECK_THROW(
       (custom_dht_profile(builtins::kad_dht, dht::mode::client,
                           dht::profile_capabilities{.peers = true, .providers = false, .values = false})),
       exceptions::invalid_options);
}

BOOST_AUTO_TEST_CASE(dht_custom_profile_rejects_overlapping_value_policy_namespaces) {
   const auto make_policy = [](std::string_view prefix) {
      return dht::value_policy{
          .key_prefix = bytes(prefix),
          .validate = [](const dht::record&, dht::value_validation_context) {},
          .select = [](std::span<const dht::record>) { return std::size_t{}; },
          .expiry = [](const dht::record&, dht::value_expiry_context context) { return context.supplied_expires_at; },
      };
   };

   BOOST_CHECK_THROW((custom_dht_profile(protocol_id{.value = "/forge/custom-values/1.0.0"}, dht::mode::server,
                                         dht::profile_capabilities{.peers = true, .providers = true, .values = true},
                                         {make_policy("/product/"), make_policy("/product/names/")})),
                     exceptions::invalid_options);
}

BOOST_AUTO_TEST_CASE(dht_codec_accepts_exact_amino_payload_boundary_and_rejects_next_byte) {
   const auto limits = amino_v1().limits;
   auto message = dht::message{
       .type = dht::message_type::get_value,
       .key_value = dht::key{.bytes = std::vector<std::uint8_t>(16'379, 0x41)},
   };

   const auto encoded = dht::codec::encode(message, limits);
   BOOST_TEST(framed_payload_size(encoded) == 16U * 1024U);
   BOOST_TEST(dht::codec::decode(encoded, limits).key_value.bytes.size() == 16'379U);

   message.key_value.bytes.push_back(0x41);
   BOOST_CHECK_THROW((dht::codec::encode(message, limits)), exceptions::invalid_options);

   auto donor_limits = limits;
   donor_limits.max_outbound_message_size = 64 * 1024;
   message.key_value.bytes.resize(32 * 1024, 0x42);
   const auto donor_encoded = dht::codec::encode(message, donor_limits);
   BOOST_TEST(framed_payload_size(donor_encoded) > limits.max_outbound_message_size);
   BOOST_TEST(dht::codec::decode(donor_encoded, limits).key_value.bytes == message.key_value.bytes);
}

BOOST_AUTO_TEST_CASE(dht_codec_keeps_ttl_field_777_golden_and_enforces_uint32) {
   const auto message = dht::message{
       .type = dht::message_type::put_value,
       .record_value =
           dht::record{
               .key_value = dht::key{.bytes = {'k'}},
               .value = {'v'},
               .ttl = std::chrono::seconds{std::numeric_limits<std::uint32_t>::max()},
           },
   };
   const auto expected = std::vector<std::uint8_t>{
       0x0f, 0x1a, 0x0d, 0x0a, 0x01, 0x6b, 0x12, 0x01, 0x76, 0xc8, 0x30, 0xff, 0xff, 0xff, 0xff, 0x0f,
   };
   const auto encoded = dht::codec::encode(message);
   BOOST_TEST(encoded == expected);
   BOOST_REQUIRE(dht::codec::decode(encoded).record_value);
   BOOST_TEST(dht::codec::decode(encoded).record_value->ttl.count() ==
              static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()));

   auto invalid = message;
   invalid.record_value->ttl = std::chrono::seconds{-1};
   BOOST_CHECK_THROW((dht::codec::encode(invalid)), exceptions::invalid_options);
   invalid.record_value->ttl =
       std::chrono::seconds{static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 1};
   BOOST_CHECK_THROW((dht::codec::encode(invalid)), exceptions::invalid_options);

   auto overflow = expected;
   overflow[11] = 0x80;
   overflow[12] = 0x80;
   overflow[13] = 0x80;
   overflow[14] = 0x80;
   overflow[15] = 0x10;
   BOOST_CHECK_THROW((dht::codec::decode(overflow)), exceptions::codec_error);
}

BOOST_AUTO_TEST_CASE(dht_codec_validates_amino_provider_multihash_and_80_byte_bound) {
   const auto amino = amino_v1();
   auto custom_limits = dht::options{};
   custom_limits.max_outbound_message_size = 32 * 1024;
   const auto custom = custom_profile(custom_limits);
   const auto maximum = forge::multiformats::multihash::identity(std::vector<std::uint8_t>(78, 0x42)).encode();
   const auto oversized = forge::multiformats::multihash::identity(std::vector<std::uint8_t>(79, 0x42)).encode();
   BOOST_REQUIRE_EQUAL(maximum.size(), 80U);
   BOOST_REQUIRE_EQUAL(oversized.size(), 81U);

   const auto valid = dht::message{
       .type = dht::message_type::get_providers,
       .key_value = dht::key{.bytes = maximum},
   };
   const auto valid_encoded = dht::codec::encode(valid, amino);
   BOOST_TEST(dht::codec::decode(valid_encoded, amino).key_value.bytes == maximum);

   const auto too_large = dht::message{
       .type = dht::message_type::get_providers,
       .key_value = dht::key{.bytes = oversized},
   };
   BOOST_CHECK_THROW((dht::codec::encode(too_large, amino)), exceptions::invalid_options);
   const auto oversized_custom = dht::codec::encode(too_large, custom.limits);
   BOOST_CHECK_THROW((dht::codec::decode(oversized_custom, amino)), exceptions::codec_error);

   const auto malformed = dht::message{
       .type = dht::message_type::add_provider,
       .key_value = dht::key{.bytes = {0x12}},
   };
   BOOST_CHECK_THROW((dht::codec::encode(malformed, amino)), exceptions::invalid_options);
   const auto malformed_custom = dht::codec::encode(malformed, custom.limits);
   BOOST_TEST(dht::codec::decode(malformed_custom, custom.limits).key_value.bytes == (std::vector<std::uint8_t>{0x12}));
   BOOST_CHECK_THROW((dht::codec::decode(malformed_custom, amino)), exceptions::codec_error);
}

BOOST_AUTO_TEST_CASE(dht_codec_checks_peer_count_and_peer_size_before_append) {
   auto strict = dht::options{};
   strict.max_outbound_message_size = 64;
   strict.max_inbound_message_size = 64;
   strict.max_closer_peers = 1;
   strict.max_provider_peers = 1;
   auto loose = strict;
   loose.max_outbound_message_size = 1024;
   loose.max_inbound_message_size = 1024;
   loose.max_provider_peers = 2;
   const auto peer = dht::peer{.id = test_peer(1)};
   const auto peers = dht::message{
       .type = dht::message_type::get_value,
       .provider_peers = {peer, dht::peer{.id = test_peer(2)}},
   };

   BOOST_CHECK_THROW((dht::codec::encode(peers, strict)), exceptions::invalid_options);
   const auto encoded = dht::codec::encode(peers, loose);
   BOOST_TEST(dht::codec::decode(encoded, strict).provider_peers.size() == 1U);

   auto oversized_peer = dht::message{
       .type = dht::message_type::find_node,
       .closer_peers = {dht::peer{.id = test_peer(3)}},
   };
   oversized_peer.closer_peers.front().endpoints.reserve(16);
   for (auto index = 0; index < 16; ++index) {
      oversized_peer.closer_peers.front().endpoints.push_back(
          parse_endpoint("/ip4/127.0.0.1/udp/4401/quic-v1/p2p/" + oversized_peer.closer_peers.front().id.to_string()));
   }
   BOOST_CHECK_THROW((dht::codec::encode(oversized_peer, strict)), exceptions::invalid_options);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace forge::net::p2p
