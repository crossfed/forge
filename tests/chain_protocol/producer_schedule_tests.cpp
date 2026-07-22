#include <boost/test/unit_test.hpp>

#include <concepts>
#include <optional>
#include <variant>

import forge.chain.protocol.block;
import forge.chain.protocol.producer_authority;
import forge.chain.protocol.producer_schedule;
import forge.codec.hex;
import forge.crypto.asymmetric;
import forge.raw.raw;
import forge.variant.described;
import forge.variant.value;

namespace protocol = forge::chain::protocol;

static_assert(
    std::same_as<decltype(protocol::block_header::new_producers), std::optional<protocol::producer_schedule>>);

BOOST_AUTO_TEST_SUITE(chain_protocol_producer_schedule_tests)

BOOST_AUTO_TEST_CASE(block_and_authority_modules_share_the_canonical_schedule_types) {
   const auto key = protocol::producer_key{
       .producer_name = {},
       .block_signing_key = protocol::public_key{std::in_place_index<0>},
   };
   const auto schedule = protocol::producer_schedule{
       .version = 7,
       .producers = {key},
   };

   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(key)) ==
              "000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(schedule)) ==
              "0700000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000");

   auto encoded = forge::variant{};
   forge::to_variant(schedule, encoded);

   auto decoded = protocol::producer_schedule{};
   forge::from_variant(encoded, decoded);
   BOOST_TEST(decoded.version == schedule.version);
   BOOST_REQUIRE(decoded.producers.size() == 1U);
   BOOST_CHECK(decoded.producers.front().producer_name == key.producer_name);
   BOOST_CHECK(decoded.producers.front().block_signing_key == key.block_signing_key);
}

BOOST_AUTO_TEST_SUITE_END()
