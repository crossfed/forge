#include <boost/test/unit_test.hpp>
#include <boost/asio/awaitable.hpp>

#include <bit>
#include <coroutine>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

import forge.asio.blocking;
import forge.asio.runtime;
import forge.chain.transaction.builder;
import forge.chain.transaction.signing;
import forge.codec.hex;
import forge.crypto.asymmetric;
import forge.raw.raw;

namespace chain_transaction = forge::chain::transaction;
namespace protocol = forge::chain::protocol;

namespace {

class local_signer final : public forge::crypto::signer::provider {
 public:
   local_signer() : key_(forge::crypto::asymmetric::private_key::generate()) {}

   [[nodiscard]] forge::crypto::signer::key_info info() const {
      return {.id = {.value = "test-key"}, .public_key = key_.get_public_key()};
   }

   [[nodiscard]] std::size_t sign_calls() const noexcept {
      return sign_calls_;
   }

   boost::asio::awaitable<std::vector<forge::crypto::signer::key_info>> keys() override {
      co_return std::vector{info()};
   }

   boost::asio::awaitable<forge::crypto::signer::key_info> describe(const forge::crypto::signer::key_id& id) override {
      BOOST_REQUIRE(id == info().id);
      co_return info();
   }

   boost::asio::awaitable<forge::crypto::signer::sign_digest_response>
   sign_digest(forge::crypto::signer::sign_digest_request request) override {
      ++sign_calls_;
      BOOST_REQUIRE(request.id == info().id);
      co_return forge::crypto::signer::sign_digest_response{
          .public_key = key_.get_public_key(),
          .signature = key_.sign_digest(request.digest),
      };
   }

 private:
   forge::crypto::asymmetric::private_key key_;
   std::size_t sign_calls_ = 0;
};

class malformed_signature_signer final : public forge::crypto::signer::provider {
 public:
   malformed_signature_signer() : key_(forge::crypto::asymmetric::private_key::generate()) {}

   [[nodiscard]] forge::crypto::signer::key_info info() const {
      return {.id = {.value = "malformed"}, .public_key = key_.get_public_key()};
   }

   boost::asio::awaitable<std::vector<forge::crypto::signer::key_info>> keys() override {
      co_return std::vector{info()};
   }

   boost::asio::awaitable<forge::crypto::signer::key_info> describe(const forge::crypto::signer::key_id&) override {
      co_return info();
   }

   boost::asio::awaitable<forge::crypto::signer::sign_digest_response>
   sign_digest(forge::crypto::signer::sign_digest_request) override {
      co_return forge::crypto::signer::sign_digest_response{
          .public_key = key_.get_public_key(),
          .signature = forge::crypto::asymmetric::k1_signature{},
      };
   }

 private:
   forge::crypto::asymmetric::private_key key_;
};

protocol::block_id reference_block(std::uint32_t number, std::uint32_t prefix) {
   auto result = protocol::block_id{};
   result._hash[0] = std::byteswap(number);
   result._hash[1] = prefix;
   return result;
}

protocol::action spring_setabi_action() {
   auto result = protocol::action{};
   result.account = protocol::make_name("eosio");
   result.name = protocol::make_name("setabi");
   result.data = {0x01, 0x02};
   return result;
}

chain_transaction::context context() {
   return chain_transaction::context{
       .chain = protocol::chain_id{"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"},
       .reference_block = reference_block(1U, 0xaabbccddU),
       .reference_time = protocol::time_point_sec{},
   };
}

} // namespace

BOOST_AUTO_TEST_SUITE(chain_transaction_tests)

BOOST_AUTO_TEST_CASE(builder_matches_spring_transaction_golden) {
   auto value =
       chain_transaction::builder{
           context(),
           chain_transaction::options{.expiration = protocol::time_point_sec{1U}},
       }
           .add_action(spring_setabi_action())
           .build();

   constexpr auto expected = "010000000100ddccbbaa00000000010000000000ea305500000000b863b2c20002010200";
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(value.value)) == expected);
   BOOST_TEST(value.value.ref_block_num == 1U);
   BOOST_TEST(value.value.ref_block_prefix == 0xaabbccddU);
}

BOOST_AUTO_TEST_CASE(builder_rounds_network_limit_to_spring_words) {
   const auto value =
       chain_transaction::builder{
           context(),
           chain_transaction::options{.expiration_seconds = 15U, .max_net_usage_bytes = 9U},
       }
           .add_action(spring_setabi_action())
           .build();

   BOOST_TEST(value.value.expiration.sec_since_epoch() == 15U);
   BOOST_TEST(value.value.max_net_usage_words.value == 2U);
}

BOOST_AUTO_TEST_CASE(builder_rejects_expired_transaction_and_rounds_maximum_network_limit_without_overflow) {
   BOOST_CHECK_THROW(((void)chain_transaction::builder{
                         context(), chain_transaction::options{.expiration = protocol::time_point_sec{}}}),
                     chain_transaction::exceptions::invalid_options);

   const auto value =
       chain_transaction::builder{
           context(),
           chain_transaction::options{.expiration_seconds = 15U,
                                      .max_net_usage_bytes = std::numeric_limits<std::uint32_t>::max()},
       }
           .add_action(spring_setabi_action())
           .build();
   BOOST_TEST(value.value.max_net_usage_words.value == 536'870'912U);
}

BOOST_AUTO_TEST_CASE(signing_uses_provider_and_canonical_digest) {
   auto signer = local_signer{};
   auto runtime = forge::asio::runtime{};
   auto value =
       chain_transaction::builder{context(), chain_transaction::options{.expiration = protocol::time_point_sec{1U}}}
           .add_action(spring_setabi_action())
           .build();
   const auto info = signer.info();

   const auto prepared = forge::asio::blocking::run(
       runtime, chain_transaction::sign(std::move(value), {{.id = info.id, .public_key = info.public_key}}, signer));

   BOOST_TEST(prepared.id == prepared.packed.id());
   BOOST_TEST(prepared.signed_value.signatures.size() == 1U);
   const auto digest = protocol::digest{"76e3d6831284af5d4056feb88dc8fa9319ff52d37383652ef5165c9913e0384a"};
   BOOST_TEST(forge::crypto::asymmetric::recover(prepared.signed_value.signatures.front(), digest) == info.public_key);
   BOOST_TEST(signer.sign_calls() == 1U);
}

BOOST_AUTO_TEST_CASE(signing_rejects_duplicate_keys_before_provider_dispatch) {
   auto signer = local_signer{};
   auto runtime = forge::asio::runtime{};
   auto value = chain_transaction::builder{context()}.add_action(spring_setabi_action()).build();
   const auto info = signer.info();
   const auto key = chain_transaction::signing_key{.id = info.id, .public_key = info.public_key};

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, chain_transaction::sign(std::move(value), {key, key}, signer)),
                     chain_transaction::exceptions::duplicate_signature);
   BOOST_TEST(signer.sign_calls() == 0U);
}

BOOST_AUTO_TEST_CASE(signing_rejects_every_invalid_key_before_provider_dispatch) {
   auto signer = local_signer{};
   auto runtime = forge::asio::runtime{};
   auto value = chain_transaction::builder{context()}.add_action(spring_setabi_action()).build();
   const auto info = signer.info();

   BOOST_CHECK_THROW(
       forge::asio::blocking::run(runtime, chain_transaction::sign(std::move(value),
                                                                   {{.id = info.id, .public_key = info.public_key},
                                                                    {.id = {}, .public_key = info.public_key}},
                                                                   signer)),
       chain_transaction::exceptions::invalid_options);
   BOOST_TEST(signer.sign_calls() == 0U);
}

BOOST_AUTO_TEST_CASE(signing_rejects_empty_key_set) {
   auto signer = local_signer{};
   auto runtime = forge::asio::runtime{};
   auto value = chain_transaction::builder{context()}.add_action(spring_setabi_action()).build();

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, chain_transaction::sign(std::move(value), {}, signer)),
                     chain_transaction::exceptions::invalid_options);
}

BOOST_AUTO_TEST_CASE(signing_translates_malformed_provider_signature_to_signer_mismatch) {
   auto signer = malformed_signature_signer{};
   auto runtime = forge::asio::runtime{};
   auto value = chain_transaction::builder{context()}.add_action(spring_setabi_action()).build();
   const auto info = signer.info();

   BOOST_CHECK_THROW(forge::asio::blocking::run(
                         runtime, chain_transaction::sign(std::move(value),
                                                          {{.id = info.id, .public_key = info.public_key}}, signer)),
                     chain_transaction::exceptions::signer_mismatch);
}

BOOST_AUTO_TEST_SUITE_END()
