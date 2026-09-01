#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import forge.chain.protocol.contract_commitment;
import forge.codec.hex;
import forge.db.authenticated.codec;
import forge.db.authenticated.hash;
import forge.db.authenticated.types;
import forge.raw.raw;

namespace {

namespace authenticated = forge::db::authenticated;
namespace protocol = forge::chain::protocol;

std::string hex(std::span<const std::byte> value) {
   auto bytes = std::vector<std::uint8_t>{};
   bytes.reserve(value.size());
   std::ranges::transform(value, std::back_inserter(bytes),
                          [](std::byte byte) { return std::to_integer<std::uint8_t>(byte); });
   return forge::codec::hex::encode(bytes);
}

std::string hex(const authenticated::digest& value) {
   return forge::codec::hex::encode(value.to_uint8_span());
}

authenticated::bytes db_bytes(std::span<const std::byte> value) {
   return {value.begin(), value.end()};
}

authenticated::bytes db_bytes(const std::vector<std::uint8_t>& value) {
   auto result = authenticated::bytes{};
   result.reserve(value.size());
   std::ranges::transform(value, std::back_inserter(result),
                          [](std::uint8_t byte) { return static_cast<std::byte>(byte); });
   return result;
}

authenticated::bytes prefix_upper_bound(authenticated::bytes prefix) {
   for (auto position = prefix.size(); position > 0U; --position) {
      const auto index = position - 1U;
      const auto value = std::to_integer<std::uint8_t>(prefix[index]);
      if (value != 0xffU) {
         prefix[index] = static_cast<std::byte>(value + 1U);
         prefix.resize(position);
         return prefix;
      }
   }
   return {};
}

authenticated::proof_leaf leaf(authenticated::bytes key, authenticated::bytes value) {
   return {
       .key = std::move(key),
       .value_hash = authenticated::hash_value(value),
       .value = std::move(value),
   };
}

authenticated::proof_leaf change_leaf(const authenticated::mutation& mutation) {
   auto value = authenticated::encode_change_value(mutation);
   return {
       .key = mutation.key,
       .value_hash = authenticated::hash_value(value),
       .value = std::move(value),
   };
}

} // namespace

BOOST_AUTO_TEST_CASE(contract_commitment_all_spine_family_key_and_value_goldens) {
   constexpr auto location = protocol::contract_table_location{
       .code = 0x0102'0304'0506'0708ULL,
       .scope = 0x1112'1314'1516'1718ULL,
       .table = 0x2122'2324'2526'2720ULL,
   };
   constexpr auto primary = std::uint64_t{0x3132'3334'3536'3738ULL};

   struct family_golden {
      protocol::contract_table_family family;
      std::string_view prefix;
   };
   constexpr auto families = std::array{
       family_golden{protocol::contract_table_family::table,
                     "0102010203040506070811121314151617182122232425262720001e"},
       family_golden{protocol::contract_table_family::primary,
                     "0102010203040506070811121314151617182122232425262720001f"},
       family_golden{protocol::contract_table_family::secondary_u64,
                     "01020102030405060708111213141516171821222324252627200020"},
       family_golden{protocol::contract_table_family::secondary_u128,
                     "01020102030405060708111213141516171821222324252627200021"},
       family_golden{protocol::contract_table_family::secondary_u256,
                     "01020102030405060708111213141516171821222324252627200022"},
       family_golden{protocol::contract_table_family::secondary_f64,
                     "01020102030405060708111213141516171821222324252627200023"},
       family_golden{protocol::contract_table_family::secondary_f128,
                     "01020102030405060708111213141516171821222324252627200024"},
   };
   for (const auto& golden : families) {
      BOOST_TEST(hex(protocol::contract_index_prefix(location, golden.family)) == golden.prefix);
   }
   BOOST_TEST(protocol::contract_table_key(location) ==
              protocol::contract_index_prefix(location, protocol::contract_table_family::table));

   BOOST_TEST(hex(protocol::contract_primary_key(location, protocol::contract_table_family::primary, primary)) ==
              "0102010203040506070811121314151617182122232425262720001f3132333435363738");

   struct secondary_golden {
      protocol::contract_table_family family;
      protocol::commitment_bytes secondary;
      std::string_view key;
   };
   const auto secondary = std::array{
       secondary_golden{
           protocol::contract_table_family::secondary_u64,
           {std::byte{0x40U}, std::byte{0x41U}, std::byte{0x42U}, std::byte{0x43U}, std::byte{0x44U}, std::byte{0x45U},
            std::byte{0x46U}, std::byte{0x47U}},
           "0102010203040506070811121314151617182122232425262720002040414243444546473132333435363738",
       },
       secondary_golden{
           protocol::contract_table_family::secondary_u128,
           {std::byte{0x40U}, std::byte{0x41U}, std::byte{0x42U}, std::byte{0x43U}, std::byte{0x44U}, std::byte{0x45U},
            std::byte{0x46U}, std::byte{0x47U}, std::byte{0x48U}, std::byte{0x49U}, std::byte{0x4aU}, std::byte{0x4bU},
            std::byte{0x4cU}, std::byte{0x4dU}, std::byte{0x4eU}, std::byte{0x4fU}},
           "01020102030405060708111213141516171821222324252627200021404142434445464748494a4b4c4d4e4f3132333435363738",
       },
       secondary_golden{
           protocol::contract_table_family::secondary_u256,
           {std::byte{0x40U}, std::byte{0x41U}, std::byte{0x42U}, std::byte{0x43U}, std::byte{0x44U}, std::byte{0x45U},
            std::byte{0x46U}, std::byte{0x47U}, std::byte{0x48U}, std::byte{0x49U}, std::byte{0x4aU}, std::byte{0x4bU},
            std::byte{0x4cU}, std::byte{0x4dU}, std::byte{0x4eU}, std::byte{0x4fU}, std::byte{0x50U}, std::byte{0x51U},
            std::byte{0x52U}, std::byte{0x53U}, std::byte{0x54U}, std::byte{0x55U}, std::byte{0x56U}, std::byte{0x57U},
            std::byte{0x58U}, std::byte{0x59U}, std::byte{0x5aU}, std::byte{0x5bU}, std::byte{0x5cU}, std::byte{0x5dU},
            std::byte{0x5eU}, std::byte{0x5fU}},
           "01020102030405060708111213141516171821222324252627200022404142434445464748494a4b4c4d4e4f5051525354555657585"
           "95a5b5c5d5e5f3132333435363738",
       },
       secondary_golden{
           protocol::contract_table_family::secondary_f64,
           {std::byte{0x7fU}, std::byte{0xf8U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
            std::byte{0x00U}, std::byte{0x01U}},
           "010201020304050607081112131415161718212223242526272000237ff80000000000013132333435363738",
       },
       secondary_golden{
           protocol::contract_table_family::secondary_f128,
           {std::byte{0x3fU}, std::byte{0xffU}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
            std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
            std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}},
           "010201020304050607081112131415161718212223242526272000243fff00000000000000000000000000003132333435363738",
       },
   };
   for (const auto& golden : secondary) {
      const auto prefix = protocol::contract_index_prefix(location, golden.family);
      const auto key = protocol::contract_secondary_key(location, golden.family, golden.secondary, primary);
      BOOST_TEST(hex(key) == golden.key);
      const auto decoded = protocol::decode_contract_secondary_key(key, prefix, golden.secondary.size());
      BOOST_REQUIRE(decoded.has_value());
      BOOST_CHECK(decoded->secondary == golden.secondary);
      BOOST_TEST(decoded->primary == primary);
   }

   const auto primary_wire = forge::raw::pack(protocol::primary_value{
       .payer = protocol::account_name{0x0102'0304'0506'0708ULL},
       .row = {0xdeU, 0xadU, 0xbeU, 0xefU},
   });
   BOOST_TEST(forge::codec::hex::encode(primary_wire) == "080706050403020104deadbeef");
   const auto table_wire = forge::raw::pack(protocol::table_value{
       .payer = protocol::account_name{0x0102'0304'0506'0708ULL},
       .count = 0x1112'1314U,
   });
   BOOST_TEST(forge::codec::hex::encode(table_wire) == "080706050403020114131211");
   const auto secondary_wire = forge::raw::pack(protocol::secondary_value{
       .payer = protocol::account_name{0x0102'0304'0506'0708ULL},
       .primary = 0x1112'1314'1516'1718ULL,
   });
   BOOST_TEST(forge::codec::hex::encode(secondary_wire) == "08070605040302011817161514131211");
}

BOOST_AUTO_TEST_CASE(contract_commitment_spine_state_root_and_proof_wire_goldens) {
   // Captured from Spine f5f8afcb927ac19890a69d04c581e80d8e8f95e8 using schema.cppm's
   // table_value(payer,uint32_t count), schema.cpp contract_table_key(), projection.cpp apply_table(),
   // and tests/unit/libraries/chain/state_commitment/projection_tests.cpp metadata projection fixtures.
   constexpr auto state_domain = std::string_view{"spine.chain.state"};
   constexpr auto location = protocol::contract_table_location{
       .code = 0x0102'0304'0506'0708ULL,
       .scope = 0x1112'1314'1516'1718ULL,
       .table = 0x2122'2324'2526'2720ULL,
   };
   const auto table_key = protocol::contract_table_key(location);
   const auto table_value = forge::raw::pack(protocol::table_value{
       .payer = protocol::account_name{0x0102'0304'0506'0708ULL},
       .count = 0x1112'1314U,
   });
   const auto primary_prefix = protocol::contract_index_prefix(location, protocol::contract_table_family::primary);
   const auto primary_key =
       protocol::contract_primary_key(location, protocol::contract_table_family::primary, 0x3132'3334'3536'3738ULL);
   const auto primary_value = forge::raw::pack(protocol::primary_value{
       .payer = protocol::account_name{0x0102'0304'0506'0708ULL},
       .row = {0xdeU, 0xadU, 0xbeU, 0xefU},
   });
   const auto table_leaf = leaf(db_bytes(table_key), db_bytes(table_value));
   const auto primary_leaf = leaf(db_bytes(primary_key), db_bytes(primary_value));
   const auto table_mutation = authenticated::mutation{.key = table_leaf.key, .value = *table_leaf.value};
   const auto primary_mutation = authenticated::mutation{.key = primary_leaf.key, .value = *primary_leaf.value};
   const auto table_change = change_leaf(table_mutation);
   const auto primary_change = change_leaf(primary_mutation);
   const auto state_tree = authenticated::canonical_tree_domain(state_domain, authenticated::proof_tree::state);
   const auto changes_tree = authenticated::canonical_tree_domain(state_domain, authenticated::proof_tree::changes);
   const auto root = authenticated::root{
       .version = 3U,
       .state_root =
           authenticated::hash_inner(state_tree, 1U, 2U, table_leaf.key, primary_leaf.key, primary_leaf.key,
                                     authenticated::hash_leaf(state_tree, table_leaf.key, table_leaf.value_hash),
                                     authenticated::hash_leaf(state_tree, primary_leaf.key, primary_leaf.value_hash)),
       .state_size = 2U,
       .change_root = authenticated::hash_inner(
           changes_tree, 1U, 2U, table_change.key, primary_change.key, primary_change.key,
           authenticated::hash_leaf(changes_tree, table_change.key, table_change.value_hash),
           authenticated::hash_leaf(changes_tree, primary_change.key, primary_change.value_hash)),
       .change_count = 2U,
   };
   const auto request = authenticated::range_request{
       .lower = table_leaf.key,
       .upper = prefix_upper_bound(db_bytes(primary_prefix)),
       .limit = 2U,
       .include_values = true,
   };
   const auto inner = authenticated::range_inner{
       .height = 1U,
       .size = 2U,
       .min_key = table_leaf.key,
       .max_key = primary_leaf.key,
       .separator = primary_leaf.key,
   };
   const auto state_proof = authenticated::range_proof{
       .anchor = root,
       .tree = authenticated::proof_tree::state,
       .request = request,
       .nodes = {inner, table_leaf, primary_leaf},
   };
   const auto changes_proof = authenticated::range_proof{
       .anchor = root,
       .tree = authenticated::proof_tree::changes,
       .request = request,
       .nodes = {inner, table_change, primary_change},
   };

   BOOST_TEST(hex(root.state_root) == "227ee31f961904d0b9c361209085ce2c7ab2ca6f8c971751c82fc8426a11c9ef");
   BOOST_TEST(hex(root.change_root) == "73ea1ffc7b07a8949cc59e4931adb9d6bbde8bcd9d7260cd02fa242f9f053a93");
   BOOST_TEST(hex(authenticated::encode(state_proof)) ==
              "0300000000000000227ee31f961904d0b9c361209085ce2c7ab2ca6f8c971751c82fc8426a11c9ef0200000000000000"
              "73ea1ffc7b07a8949cc59e4931adb9d6bbde8bcd9d7260cd02fa242f9f053a93020000000000000000011c0102010203"
              "040506070811121314151617182122232425262720001e011c01020102030405060708111213141516171821222324252627"
              "2000200200000001000302010002000000000000001c0102010203040506070811121314151617182122232425262720001e"
              "240102010203040506070811121314151617182122232425262720001f313233343536373824010201020304050607081112"
              "1314151617182122232425262720001f3132333435363738011c010201020304050607081112131415161718212223242526"
              "2720001ed85351834fd9ea382bf8155629012823ab75faf531b473a53a08e547f69bd236010c080706050403020114131211"
              "01240102010203040506070811121314151617182122232425262720001f31323334353637381c9f7a30df80148dcbbc7d63"
              "b5757b11167a484d7a1ab63cb772761b09adc8a5010d080706050403020104deadbeef");
   BOOST_TEST(hex(authenticated::encode(changes_proof)) ==
              "0300000000000000227ee31f961904d0b9c361209085ce2c7ab2ca6f8c971751c82fc8426a11c9ef0200000000000000"
              "73ea1ffc7b07a8949cc59e4931adb9d6bbde8bcd9d7260cd02fa242f9f053a93020000000000000001011c0102010203"
              "040506070811121314151617182122232425262720001e011c01020102030405060708111213141516171821222324252627"
              "2000200200000001000302010002000000000000001c0102010203040506070811121314151617182122232425262720001e"
              "240102010203040506070811121314151617182122232425262720001f313233343536373824010201020304050607081112"
              "1314151617182122232425262720001f3132333435363738011c010201020304050607081112131415161718212223242526"
              "2720001ed10f31f5e262af74fcfe5894ca593de87d788104c242355ca1f09af6bea935d9010d010807060504030201141312"
              "1101240102010203040506070811121314151617182122232425262720001f3132333435363738217ac9c575544477817713"
              "e8d56b629470839bdcfdc5c785151c1fb330c11e6d010e01080706050403020104deadbeef");
}
