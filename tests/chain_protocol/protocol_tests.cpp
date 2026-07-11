#include "spring_fixtures.hpp"

#include <boost/test/unit_test.hpp>

#include <array>
#include <concepts>
#include <deque>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import forge.crypto.asymmetric;
import forge.crypto.secp256k1;
import forge.crypto.sha256;
import forge.compression.exceptions;
import forge.raw.raw;
import forge.raw.exceptions;
import forge.variant.exceptions;
import forge.variant.value;
import forge.chain.core.merkle;
import forge.chain.protocol.abi;
import forge.chain.protocol.block;
import forge.chain.protocol.fixed_key;
import forge.chain.protocol.system;
import forge.chain.protocol.transaction;
import forge.chain.protocol.types;

namespace core = forge::chain::core;
namespace protocol = forge::chain::protocol;
namespace spring = forge::tests::spring_fixtures;

namespace {

std::string expected(std::string_view value) {
   return std::string{value};
}

std::string hex(std::span<const std::uint8_t> bytes) {
   std::ostringstream out;
   out << std::hex << std::setfill('0');
   for (const auto byte : bytes) {
      out << std::setw(2) << static_cast<unsigned>(byte);
   }
   return out.str();
}

protocol::bytes unhex(std::string_view value) {
   auto out = protocol::bytes{};
   out.reserve(value.size() / 2);
   for (auto index = std::size_t{0}; index < value.size(); index += 2) {
      const auto byte = std::string{value.substr(index, 2)};
      out.push_back(static_cast<std::uint8_t>(std::stoi(byte, nullptr, 16)));
   }
   return out;
}

template <typename T> std::string pack_hex(const T& value) {
   return hex(forge::raw::pack(value));
}

std::string word_hex(protocol::uint128_t value) {
   std::ostringstream out;
   out << std::hex << std::setfill('0') << std::setw(16) << static_cast<std::uint64_t>(value >> 64U) << std::setw(16)
       << static_cast<std::uint64_t>(value);
   return out.str();
}

template <std::size_t Size> std::string backing_hex(const protocol::fixed_key<Size>& value) {
   auto result = std::string{};
   for (const auto word : value.get_array()) {
      result += word_hex(word);
   }
   return result;
}

template <typename Key, typename Word>
concept supports_single_word_factory = requires(Word word) {
   { Key::template make_from_word_sequence<Word>(word) } -> std::same_as<Key>;
};

static_assert(std::constructible_from<protocol::key256, std::array<std::uint32_t, 5>>);
static_assert(!std::constructible_from<protocol::key256, std::array<protocol::uint128_t, 1>>);
static_assert(supports_single_word_factory<protocol::key256, protocol::uint128_t>);
static_assert(!supports_single_word_factory<protocol::key256, std::int64_t>);
static_assert(protocol::fixed_key<1>::num_words() == 1U && protocol::fixed_key<1>::padded_bytes() == 15U);
static_assert(protocol::fixed_key<20>::num_words() == 2U && protocol::fixed_key<20>::padded_bytes() == 12U);
static_assert(protocol::fixed_key<32>::num_words() == 2U && protocol::fixed_key<32>::padded_bytes() == 0U);
static_assert(protocol::fixed_key<64>::num_words() == 4U && protocol::fixed_key<64>::padded_bytes() == 0U);

protocol::signature parse_spring_signature(std::string_view value) {
   return forge::crypto::asymmetric::encoding::antelope().parse_signature(value);
}

protocol::public_key parse_spring_public_key(std::string_view value) {
   return forge::crypto::asymmetric::encoding::antelope().parse_public(value);
}

std::string format_spring_public_key(const protocol::public_key& value) {
   return forge::crypto::asymmetric::encoding::antelope().format(value);
}

protocol::action make_setabi_action() {
   protocol::action value;
   value.account = protocol::make_name("eosio");
   value.name = protocol::make_name("setabi");
   value.data = {char{0x01}, char{0x02}};
   return value;
}

std::vector<protocol::bytes> make_context_free_data() {
   return {{char{0x03}, char{0x04}}};
}

protocol::transaction make_reference_transaction() {
   protocol::transaction value;
   value.expiration = std::chrono::sys_seconds{};
   value.ref_block_num = 1;
   value.ref_block_prefix = 0xaabbccdd;
   value.actions = {make_setabi_action()};
   return value;
}

protocol::signed_transaction make_reference_signed_transaction() {
   auto value = protocol::signed_transaction{};
   static_cast<protocol::transaction&>(value) = make_reference_transaction();
   value.signatures = {parse_spring_signature(spring::transaction_signature)};
   value.context_free_data = make_context_free_data();
   return value;
}

protocol::abi_def make_reference_abi() {
   return protocol::abi_def{
       .version = "eosio::abi/1.2",
       .types = {protocol::type_def{.new_type_name = "account_name", .type = "name"}},
       .actions = {protocol::action_def{
           .name = protocol::make_name("setabi"),
           .type = "setabi",
       }},
   };
}

std::string legacy_abi_hex() {
   constexpr auto empty_optional_vector_hex_size = std::size_t{2};
   return std::string{spring::abi_raw.substr(0, spring::abi_raw.size() - 2U * empty_optional_vector_hex_size)};
}

protocol::block_header make_reference_block_header() {
   protocol::block_header header;
   header.timestamp = protocol::block_timestamp{1};
   header.producer = protocol::make_name("eosio");
   header.confirmed = 0;
   header.previous = {};
   header.transaction_mroot = protocol::calculate_transaction_id(make_reference_transaction());
   header.action_mroot = {};
   return header;
}

protocol::transaction_receipt make_reference_receipt() {
   protocol::transaction_receipt receipt;
   receipt.status = protocol::transaction_receipt::status::executed;
   receipt.trx = protocol::calculate_transaction_id(make_reference_transaction());
   return receipt;
}

protocol::transaction_receipt make_receipt(std::uint32_t cpu_usage_us) {
   auto value = protocol::transaction_receipt{};
   value.status = protocol::transaction_receipt::status::executed;
   value.cpu_usage_us = cpu_usage_us;
   value.trx = protocol::transaction_id::hash("transaction-" + std::to_string(cpu_usage_us));
   return value;
}

protocol::signed_block_header make_reference_signed_block_header() {
   auto header = protocol::signed_block_header{};
   static_cast<protocol::block_header&>(header) = make_reference_block_header();
   header.producer_signature = parse_spring_signature(spring::block_signature);
   return header;
}

protocol::signed_block make_reference_signed_block() {
   auto block = protocol::signed_block{};
   static_cast<protocol::signed_block_header&>(block) = make_reference_signed_block_header();
   block.transactions.emplace_back(make_reference_receipt());
   block.block_extensions = {{7, {char{0x0a}, char{0x0b}}}};
   return block;
}

} // namespace

BOOST_AUTO_TEST_SUITE(forge_chain_protocol_compatibility)

BOOST_AUTO_TEST_CASE(fixed_key_matches_donor_word_and_byte_order) {
   const auto high = static_cast<protocol::uint128_t>(0x0102030405060708ULL) << 64U |
                     static_cast<protocol::uint128_t>(0x1112131415161718ULL);
   const auto low = static_cast<protocol::uint128_t>(0x2122232425262728ULL) << 64U |
                    static_cast<protocol::uint128_t>(0x3132333435363738ULL);
   const auto value = protocol::key256{std::array<protocol::uint128_t, 2>{high, low}};

   BOOST_TEST(hex(value.extract_as_byte_array()) == "0102030405060708111213141516171821222324252627283132333435363738");
   BOOST_TEST(pack_hex(value) == "0102030405060708111213141516171821222324252627283132333435363738");
   BOOST_TEST((value == forge::raw::unpack<protocol::key256>(forge::raw::pack(value))));

   const auto variant = forge::variant{value};
   BOOST_TEST(variant.get_string() == "0102030405060708111213141516171821222324252627283132333435363738");
   BOOST_TEST((variant.as<protocol::key256>() == value));
}

BOOST_AUTO_TEST_CASE(fixed_key_partial_word_sequences_match_cdt_fixed_bytes) {
   const auto u8 = protocol::key256::make_from_word_sequence<std::uint8_t>(std::uint8_t{1U});
   const auto u16 = protocol::key256::make_from_word_sequence<std::uint16_t>(std::uint16_t{1U});
   const auto u32 = protocol::key256::make_from_word_sequence<std::uint32_t>(std::uint32_t{1U});
   const auto u32_pair = protocol::key256::make_from_word_sequence<std::uint32_t>(std::uint32_t{1U}, std::uint32_t{2U});
   const auto u32_triple = protocol::key256::make_from_word_sequence<std::uint32_t>(
       std::uint32_t{1U}, std::uint32_t{2U}, std::uint32_t{3U});
   const auto u32_full = protocol::key256::make_from_word_sequence<std::uint32_t>(std::uint32_t{1U}, std::uint32_t{2U},
                                                                                  std::uint32_t{3U}, std::uint32_t{4U});
   const auto u32_crossing = protocol::key256::make_from_word_sequence<std::uint32_t>(
       std::uint32_t{1U}, std::uint32_t{2U}, std::uint32_t{3U}, std::uint32_t{4U}, std::uint32_t{5U});
   const auto u64 = protocol::key256::make_from_word_sequence<std::uint64_t>(std::uint64_t{1U});
   const auto u64_full = protocol::key256::make_from_word_sequence<std::uint64_t>(std::uint64_t{1U}, std::uint64_t{2U},
                                                                                  std::uint64_t{3U}, std::uint64_t{4U});
   const auto u128 = protocol::key256::make_from_word_sequence<protocol::uint128_t>(protocol::uint128_t{1U});

   BOOST_TEST(backing_hex(u8) == "0100000000000000000000000000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u16) == "0000000000000001000000000000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u32) == "0000000000000000000100000000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u32_pair) == "0000000000000100000002000000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u32_triple) == "0000000100000002000000030000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u32_full) == "0000000100000002000000030000000400000000000000000000000000000000");
   BOOST_TEST(backing_hex(u32_crossing) == "0000000100000002000000030000000400000000000000000005000000000000");
   BOOST_TEST(backing_hex(u64) == "0000000000000001000000000000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u64_full) == "0000000000000001000000000000000200000000000000030000000000000004");
   BOOST_TEST(backing_hex(u128) == "0000000000000000000000000000000100000000000000000000000000000000");

   BOOST_TEST((u32_crossing == protocol::key256{std::array<std::uint32_t, 5>{1U, 2U, 3U, 4U, 5U}}));
   BOOST_TEST(hex(u64_full.extract_as_byte_array()) ==
              "0000000000000001000000000000000200000000000000030000000000000004");
}

BOOST_AUTO_TEST_CASE(fixed_key_orders_lexicographically_and_rejects_invalid_text) {
   const auto lower = protocol::key256::make_from_word_sequence<std::uint64_t>(std::uint64_t{1U}, std::uint64_t{2U},
                                                                               std::uint64_t{3U}, std::uint64_t{4U});
   const auto higher = protocol::key256::make_from_word_sequence<std::uint64_t>(std::uint64_t{1U}, std::uint64_t{2U},
                                                                                std::uint64_t{3U}, std::uint64_t{5U});
   BOOST_TEST(lower < higher);

   BOOST_CHECK_THROW(forge::variant{"00"}.as<protocol::key256>(), forge::variant_exceptions::decode_error);
   BOOST_CHECK_THROW(forge::variant{std::string(63U, '0') + "x"}.as<protocol::key256>(),
                     forge::variant_exceptions::decode_error);
}

BOOST_AUTO_TEST_CASE(fixed_key_supports_exact_bytes_padding_and_truncated_raw_rejection) {
   using key160 = protocol::fixed_key<20>;
   static_assert(key160::num_words() == 2U);
   static_assert(key160::padded_bytes() == 12U);

   const auto bytes = std::array<std::uint8_t, 20>{
       0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
       0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
   };
   const auto value = key160{bytes};
   BOOST_TEST(value.extract_as_byte_array() == bytes);
   BOOST_TEST(pack_hex(value) == "000102030405060708090a0b0c0d0e0f10111213");

   auto truncated = forge::raw::pack(value);
   truncated.pop_back();
   BOOST_CHECK_THROW((void)forge::raw::unpack<key160>(truncated), forge::raw::exceptions::codec_error);
}

BOOST_AUTO_TEST_CASE(fixed_key_padded_storage_and_order_match_cdt_fixed_bytes) {
   using key160 = protocol::fixed_key<20>;
   const auto first = static_cast<protocol::uint128_t>(0x0102030405060708ULL) << 64U |
                      static_cast<protocol::uint128_t>(0x1112131415161718ULL);
   const auto second = static_cast<protocol::uint128_t>(0x2122232425262728ULL) << 64U |
                       static_cast<protocol::uint128_t>(0x3132333435363738ULL);
   const auto value = key160{std::array<protocol::uint128_t, 2>{first, second}};

   BOOST_TEST(backing_hex(value) == "0102030405060708111213141516171821222324252627283132333435363738");
   BOOST_TEST(hex(value.extract_as_byte_array()) == "0102030405060708111213141516171821222324");

   const auto bytes = std::array<std::uint8_t, 20>{
       0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
       0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
   };
   const auto canonical = key160{bytes};
   BOOST_TEST(backing_hex(canonical) == "000102030405060708090a0b0c0d0e0f10111213000000000000000000000000");
   BOOST_TEST((canonical == forge::raw::unpack<key160>(forge::raw::pack(canonical))));

   const auto visible = static_cast<protocol::uint128_t>(0xaabbccddULL) << 96U;
   const auto lower = key160{std::array<protocol::uint128_t, 2>{0U, visible | 1U}};
   const auto higher = key160{std::array<protocol::uint128_t, 2>{0U, visible | 2U}};
   BOOST_TEST(lower.extract_as_byte_array() == higher.extract_as_byte_array());
   BOOST_TEST(lower < higher);
}

BOOST_AUTO_TEST_CASE(fixed_key_zero_size_matches_donor_value_semantics) {
   using empty_key = protocol::fixed_key<0>;
   static_assert(empty_key::num_words() == 0U);
   static_assert(empty_key::padded_bytes() == 0U);

   const auto value = empty_key{};
   BOOST_TEST(value.get_array().empty());
   BOOST_TEST(value.extract_as_byte_array().empty());
   BOOST_TEST(forge::raw::pack(value).empty());
   BOOST_TEST((forge::raw::unpack<empty_key>(forge::raw::bytes{}) == value));

   const auto variant = forge::variant{value};
   BOOST_TEST(variant.get_string().empty());
   BOOST_TEST((variant.as<empty_key>() == value));
}

BOOST_AUTO_TEST_CASE(name_symbol_and_asset_match_spring_fixtures) {
   const auto eosio = protocol::make_name("eosio");
   const auto active = protocol::make_name("active");

   BOOST_TEST(eosio.value == spring::name_eosio_value);
   BOOST_TEST(active.value == spring::name_active_value);
   BOOST_TEST(protocol::to_string(eosio) == "eosio");
   BOOST_TEST(pack_hex(eosio) == expected(spring::name_eosio_raw));

   const auto token = protocol::asset{42, protocol::make_symbol("SYS", 4)};
   BOOST_TEST(pack_hex(token) == expected(spring::asset_raw));
}

BOOST_AUTO_TEST_CASE(name_rejects_high_valued_thirteenth_character) {
   BOOST_CHECK_NO_THROW((void)protocol::make_name("abcdefghijklj"));
   BOOST_CHECK_THROW(protocol::make_name("abcdefghijklk"), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::make_name("abcdefghijklz"), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(asset_variant_text_preserves_precision) {
   auto variant = forge::variant{};

   protocol::to_variant(protocol::asset{42, protocol::make_symbol("SYS", 4)}, variant);
   BOOST_TEST(variant.as_string() == "0.0042 SYS");

   protocol::to_variant(protocol::asset{42, protocol::make_symbol("SYS", 0)}, variant);
   BOOST_TEST(variant.as_string() == "42 SYS");

   protocol::to_variant(protocol::asset{-42, protocol::make_symbol("SYS", 4)}, variant);
   BOOST_TEST(variant.as_string() == "-0.0042 SYS");
}

BOOST_AUTO_TEST_CASE(symbol_and_asset_variant_parse_canonical_text) {
   auto symbol = protocol::symbol{};
   protocol::from_variant(forge::variant{"4,SYS"}, symbol);
   BOOST_TEST(symbol.raw() == protocol::make_symbol("SYS", 4).raw());

   auto asset = protocol::asset{};
   protocol::from_variant(forge::variant{"0.0042 SYS"}, asset);
   const auto fractional_asset = protocol::asset{42, protocol::make_symbol("SYS", 4)};
   BOOST_TEST(asset.amount == fractional_asset.amount);
   BOOST_TEST(asset.sym.raw() == fractional_asset.sym.raw());

   protocol::from_variant(forge::variant{"42 SYS"}, asset);
   const auto whole_asset = protocol::asset{42, protocol::make_symbol("SYS", 0)};
   BOOST_TEST(asset.amount == whole_asset.amount);
   BOOST_TEST(asset.sym.raw() == whole_asset.sym.raw());

   protocol::from_variant(forge::variant{"-0.0042 SYS"}, asset);
   const auto negative_asset = protocol::asset{-42, protocol::make_symbol("SYS", 4)};
   BOOST_TEST(asset.amount == negative_asset.amount);
   BOOST_TEST(asset.sym.raw() == negative_asset.sym.raw());
}

BOOST_AUTO_TEST_CASE(asset_variant_parse_rejects_invalid_text) {
   auto asset = protocol::asset{};

   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"0.0042"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"0.0042 sys"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"0. SYS"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{".0042 SYS"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"0.0042 SYS extra"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"+1 SYS"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"9223372036854775808 SYS"}, asset), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(action_transaction_and_signed_transaction_match_spring_fixtures) {
   const auto action = make_setabi_action();
   BOOST_TEST(pack_hex(action) == expected(spring::action_raw));

   const auto trx = make_reference_transaction();
   BOOST_TEST(protocol::pack_transaction(trx).size() == 36U);
   BOOST_TEST(pack_hex(trx) == expected(spring::transaction_raw));
   BOOST_TEST(protocol::calculate_transaction_id(trx).str() == expected(spring::transaction_id));

   const auto signed_trx = make_reference_signed_transaction();
   BOOST_TEST(pack_hex(signed_trx) == expected(spring::signed_transaction_raw));

   const auto packed = protocol::packed_transaction{signed_trx};
   BOOST_TEST(pack_hex(packed) == expected(spring::packed_transaction_raw));
   BOOST_TEST(packed.id().str() == expected(spring::transaction_id));
   BOOST_TEST(packed.packed_digest().str() == expected(spring::packed_transaction_digest));
}

BOOST_AUTO_TEST_CASE(zlib_packed_transaction_matches_spring_and_unpacks_from_wire) {
   const auto signed_trx = make_reference_signed_transaction();
   const auto packed = protocol::packed_transaction{signed_trx, protocol::packed_transaction::compression::zlib};

   BOOST_TEST(pack_hex(packed) == expected(spring::packed_transaction_zlib_raw));
   BOOST_TEST(packed.id().str() == expected(spring::transaction_id));
   BOOST_TEST(packed.packed_digest().str() == expected(spring::packed_transaction_zlib_digest));
   BOOST_TEST(pack_hex(packed.get_signed_transaction()) == expected(spring::signed_transaction_raw));

   const auto unpacked = forge::raw::unpack<protocol::packed_transaction>(unhex(spring::packed_transaction_zlib_raw));
   BOOST_TEST(unpacked.id().str() == expected(spring::transaction_id));
   BOOST_TEST(unpacked.packed_digest().str() == expected(spring::packed_transaction_zlib_digest));
   BOOST_TEST(pack_hex(unpacked.get_signed_transaction()) == expected(spring::signed_transaction_raw));
}

BOOST_AUTO_TEST_CASE(packed_transaction_unknown_compression_is_typed_failure) {
   auto packed = protocol::packed_transaction{make_reference_signed_transaction()};
   packed.compression = static_cast<decltype(packed.compression)>(0xff);

   BOOST_CHECK_THROW((void)packed.get_signed_transaction(), forge::compression::exceptions::invalid_input);
}

BOOST_AUTO_TEST_CASE(transaction_signature_preimage_digest_and_spring_signature_are_compatible) {
   const auto trx = make_reference_transaction();
   const auto chain_id = protocol::chain_id{std::string{spring::chain_id}};
   const auto cfd = make_context_free_data();

   BOOST_TEST(hex(protocol::signature_preimage(chain_id, trx, cfd)) ==
              expected(spring::transaction_signature_preimage));
   BOOST_TEST(protocol::signature_digest(chain_id, trx, cfd).str() == expected(spring::transaction_signature_digest));

   const auto signature = parse_spring_signature(spring::transaction_signature);
   const auto recovered = protocol::public_key{signature, core::digest{std::string{spring::transaction_signature_digest}}};
   BOOST_TEST(format_spring_public_key(recovered) == expected(spring::test_public_key));
}

BOOST_AUTO_TEST_CASE(abi_and_system_actions_match_spring_fixtures) {
   BOOST_TEST(pack_hex(make_reference_abi()) == expected(spring::abi_raw));

   const auto setabi = protocol::setabi{
       .account = protocol::make_name("eosio"),
       .abi = {char{0x0a}, char{0x0b}},
   };
   BOOST_TEST(pack_hex(setabi) == expected(spring::setabi_raw));
}

BOOST_AUTO_TEST_CASE(legacy_abi_unpack_normalizes_tail_fields_on_pack) {
   const auto legacy_hex = legacy_abi_hex();
   const auto legacy_bytes = unhex(legacy_hex);

   const auto unpacked = forge::raw::unpack<protocol::abi_def>(legacy_bytes);

   BOOST_TEST(unpacked.variants.value.empty());
   BOOST_TEST(unpacked.action_results.value.empty());
   BOOST_TEST(pack_hex(unpacked) == expected(spring::abi_raw));
}

BOOST_AUTO_TEST_CASE(may_not_exist_variant_conversion_uses_value) {
   auto value = protocol::may_not_exist<std::vector<protocol::variant_def>>{};

   auto encoded = forge::variant{};
   protocol::to_variant(value, encoded);

   auto decoded = protocol::may_not_exist<std::vector<protocol::variant_def>>{};
   protocol::from_variant(encoded, decoded);

   BOOST_TEST(encoded.get_array().empty());
   BOOST_TEST(decoded.value.empty());
}

BOOST_AUTO_TEST_CASE(abi_variant_schema_uses_spring_field_names) {
   auto abi = protocol::abi_def{};
   abi.version = "eosio::abi/1.2";
   abi.tables = {protocol::table_def{
       .name = protocol::make_name("accounts"),
       .index_type = "i64",
       .key_names = {"owner"},
       .key_types = {"name"},
       .type = "account",
   }};
   abi.action_results.value = {protocol::action_result_def{
       .name = protocol::make_name("get"),
       .result_type = "account",
   }};

   auto encoded = forge::variant{};
   protocol::to_variant(abi, encoded);

   const auto& object = encoded.get_object();
   const auto& table = object["tables"].get_array().front().get_object();
   BOOST_TEST(table.contains("index_type"));
   BOOST_TEST(!table.contains("index"));
   BOOST_TEST(table["index_type"].as_string() == "i64");

   const auto& action_result = object["action_results"].get_array().front().get_object();
   BOOST_TEST(action_result.contains("result_type"));
   BOOST_TEST(!action_result.contains("result"));
   BOOST_TEST(action_result["result_type"].as_string() == "account");
}

BOOST_AUTO_TEST_CASE(abi_variant_roundtrip_preserves_wire_compatibility) {
   const auto reference = make_reference_abi();

   auto encoded = forge::variant{};
   protocol::to_variant(reference, encoded);

   auto decoded = protocol::abi_def{};
   protocol::from_variant(encoded, decoded);

   BOOST_TEST(pack_hex(decoded) == expected(spring::abi_raw));
}

BOOST_AUTO_TEST_CASE(legacy_abi_variant_conversion_uses_empty_tail_fields) {
   const auto legacy_hex = legacy_abi_hex();
   const auto unpacked = forge::raw::unpack<protocol::abi_def>(unhex(legacy_hex));

   auto encoded = forge::variant{};
   protocol::to_variant(unpacked, encoded);

   const auto& object = encoded.get_object();
   BOOST_TEST(object.contains("variants"));
   BOOST_TEST(object.contains("action_results"));
   BOOST_TEST(object["variants"].get_array().empty());
   BOOST_TEST(object["action_results"].get_array().empty());

   auto decoded = protocol::abi_def{};
   protocol::from_variant(encoded, decoded);

   BOOST_TEST(decoded.variants.value.empty());
   BOOST_TEST(decoded.action_results.value.empty());
   BOOST_TEST(pack_hex(decoded) == expected(spring::abi_raw));
}

BOOST_AUTO_TEST_CASE(block_header_receipt_and_signed_block_match_spring_fixtures) {
   const auto header = make_reference_block_header();

   BOOST_TEST(pack_hex(header) == expected(spring::block_header_raw));
   BOOST_TEST(hex(protocol::signature_preimage(header)) == expected(spring::block_header_signature_preimage));
   BOOST_TEST(protocol::block_digest(header).str() == expected(spring::block_digest));
   BOOST_TEST(header.calculate_block_num() == spring::block_num_from_id);
   BOOST_TEST(protocol::calculate_block_num(header) == spring::block_num_from_id);
   BOOST_TEST(protocol::calculate_block_id(header).str() == expected(spring::block_id));
   BOOST_TEST(protocol::calculate_block_num_from_id(protocol::calculate_block_id(header)) == spring::block_num_from_id);

   const auto signature = parse_spring_signature(spring::block_signature);
   const auto recovered = protocol::public_key{signature, protocol::calculate_block_id(header)};
   BOOST_TEST(format_spring_public_key(recovered) == expected(spring::test_public_key));

   const auto signed_header = make_reference_signed_block_header();
   BOOST_TEST(pack_hex(signed_header) == expected(spring::signed_block_header_raw));

   const auto receipt = make_reference_receipt();
   BOOST_TEST(pack_hex(receipt) == expected(spring::transaction_receipt_raw));
   BOOST_TEST(protocol::transaction_receipt_digest(receipt).str() == expected(spring::transaction_receipt_digest));

   const auto block = make_reference_signed_block();
   BOOST_TEST(pack_hex(block) == expected(spring::signed_block_raw));
}

BOOST_AUTO_TEST_CASE(transaction_mroot_uses_core_merkle_over_receipt_digests) {
   auto receipts = std::deque<protocol::transaction_receipt>{};
   BOOST_TEST(protocol::calculate_transaction_mroot(receipts) == core::digest{});

   receipts.push_back(make_receipt(1U));
   BOOST_TEST(protocol::calculate_transaction_mroot(receipts) == receipts.front().digest());

   receipts.push_back(make_receipt(2U));
   receipts.push_back(make_receipt(3U));
   const auto digests = std::array{
      receipts[0].digest(),
      receipts[1].digest(),
      receipts[2].digest(),
   };
   BOOST_TEST(protocol::calculate_transaction_mroot(receipts) == core::calculate_merkle_root(digests));
}

BOOST_AUTO_TEST_CASE(forge_secp256k1_is_the_crypto_surface_for_runtime_signatures) {
   const auto private_key = forge::crypto::asymmetric::private_key::generate();
   const auto digest = forge::crypto::sha256{std::string{spring::transaction_signature_digest}};
   const auto signature = private_key.sign_digest(digest);
   const auto public_key = private_key.get_public_key();
   const auto recovered_key = forge::crypto::asymmetric::public_key{signature, digest};
   const auto signature_text = signature.to_string();
   const auto public_key_text = public_key.to_string();
   const auto signature_bytes = forge::raw::pack(signature);
   const auto public_key_bytes = forge::raw::pack(public_key);
   const auto parsed_signature = forge::crypto::asymmetric::signature{signature_text};
   const auto parsed_public_key = forge::crypto::asymmetric::public_key{public_key_text};
   const auto unpacked_signature = forge::raw::unpack<forge::crypto::asymmetric::signature>(signature_bytes);
   const auto unpacked_public_key = forge::raw::unpack<forge::crypto::asymmetric::public_key>(public_key_bytes);

   BOOST_TEST(static_cast<int>(signature.type()) == static_cast<int>(forge::crypto::asymmetric::algorithm::secp256k1));
   BOOST_TEST(recovered_key == public_key);
   BOOST_TEST(parsed_signature.to_string() == signature_text);
   BOOST_TEST(parsed_public_key.to_string() == public_key_text);
   BOOST_TEST(unpacked_signature.to_string() == signature_text);
   BOOST_TEST(unpacked_public_key.to_string() == public_key_text);

   const auto spring_public_key = parse_spring_public_key(spring::test_public_key);
   BOOST_TEST(format_spring_public_key(spring_public_key) == expected(spring::test_public_key));
}

BOOST_AUTO_TEST_SUITE_END()
