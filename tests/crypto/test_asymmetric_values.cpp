#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>
#include <variant>

import forge.crypto.asymmetric.values;
import forge.raw.raw;

BOOST_AUTO_TEST_SUITE(crypto_asymmetric_values)

BOOST_AUTO_TEST_CASE(binary_values_keep_algorithm_and_raw_contracts) {
   using namespace forge::crypto::asymmetric;

   static_assert(static_cast<std::int32_t>(algorithm::secp256k1) == 0);
   static_assert(static_cast<std::int32_t>(algorithm::p256) == 1);
   static_assert(static_cast<std::int32_t>(algorithm::webauthn) == 2);
   static_assert(static_cast<std::int32_t>(algorithm::ed25519) == 3);
   static_assert(static_cast<std::int32_t>(algorithm::rsa) == 4);

   auto data = ecc_public_key{};
   data.front() = 2;
   const auto key = public_key{k1_public_key{data}};
   const auto packed = forge::raw::pack(key);
   const auto unpacked = forge::raw::unpack<public_key>(packed);

   BOOST_CHECK(type(key) == algorithm::secp256k1);
   BOOST_CHECK(std::get<k1_public_key>(unpacked).data == data);
}

BOOST_AUTO_TEST_SUITE_END()
