#include <boost/test/unit_test.hpp>

#include <variant>

import forge.crypto.asymmetric;
import forge.crypto.pki.der;

BOOST_AUTO_TEST_SUITE(crypto_pki)

BOOST_AUTO_TEST_CASE(der_roundtrip_preserves_public_key) {
   const auto private_key = forge::crypto::asymmetric::private_key::generate_p256();
   const auto expected = private_key.get_public_key();
   const auto encoded = forge::crypto::pki::der::write_public_key(expected);
   const auto decoded = forge::crypto::pki::der::read_public_key(encoded);

   BOOST_CHECK(decoded == expected);
}

BOOST_AUTO_TEST_SUITE_END()
