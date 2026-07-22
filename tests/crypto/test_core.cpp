#include <boost/test/unit_test.hpp>

#include <type_traits>

import forge.crypto.core.random;
import forge.crypto.core.secret_bytes;
import forge.crypto.core.types;

BOOST_AUTO_TEST_SUITE(crypto_core)

BOOST_AUTO_TEST_CASE(random_and_secret_bytes_preserve_core_contract) {
   static_assert(!std::is_copy_constructible_v<forge::crypto::core::secret_bytes>);
   static_assert(std::is_move_constructible_v<forge::crypto::core::secret_bytes>);

   const auto random = forge::crypto::core::random_bytes(32U);
   BOOST_TEST(random.size() == 32U);

   auto secret = forge::crypto::core::secret_bytes{forge::crypto::core::bytes{1U, 2U, 3U}};
   BOOST_TEST(secret.size() == 3U);
   BOOST_TEST(secret.span().front() == 1U);
}

BOOST_AUTO_TEST_SUITE_END()
