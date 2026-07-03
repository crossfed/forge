#include "spring_fixtures.hpp"

#include <boost/test/unit_test.hpp>

#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import forge.crypto.asymmetric;
import forge.crypto.secp256k1;
import forge.crypto.sha256;
import forge.raw.raw;
import forge.chain.abi;
import forge.chain.block;
import forge.chain.types;
import forge.chain.system;
import forge.chain.transaction;

namespace protocol = forge::chain;
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

std::string hex(const std::vector<char>& bytes) {
   return hex(std::span{
      reinterpret_cast<const std::uint8_t*>(bytes.data()),
      bytes.size(),
   });
}

template <typename T>
std::string pack_hex(const T& value) {
   return hex(forge::raw::pack(value));
}

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

BOOST_AUTO_TEST_SUITE(forge_chain_spring_compatibility)

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

BOOST_AUTO_TEST_CASE(transaction_signature_preimage_digest_and_spring_signature_are_compatible) {
   const auto trx = make_reference_transaction();
   const auto chain_id = protocol::chain_id{std::string{spring::chain_id}};
   const auto cfd = make_context_free_data();

   BOOST_TEST(hex(protocol::signature_preimage(chain_id, trx, cfd)) ==
              expected(spring::transaction_signature_preimage));
   BOOST_TEST(protocol::signature_digest(chain_id, trx, cfd).str() ==
              expected(spring::transaction_signature_digest));

   const auto signature = parse_spring_signature(spring::transaction_signature);
   const auto recovered = protocol::public_key{signature, protocol::digest{std::string{spring::transaction_signature_digest}}};
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
