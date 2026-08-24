#include <boost/test/unit_test.hpp>

#include <array>
#include <string>
#include <type_traits>
#include <utility>

import forge.crypto.core.constant_time;
import forge.crypto.core.random;
import forge.crypto.core.secret_bytes;
import forge.crypto.core.secret_string;
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

BOOST_AUTO_TEST_CASE(secure_erase_clears_owned_buffers) {
   auto bytes = forge::crypto::core::bytes{1U, 2U, 3U};
   auto text = std::string{"private material"};

   forge::crypto::core::secure_erase(bytes);
   forge::crypto::core::secure_erase(text);

   BOOST_TEST(bytes.empty());
   BOOST_TEST(text.empty());
}

BOOST_AUTO_TEST_CASE(constant_time_compare_requires_equal_bytes_and_length) {
   constexpr auto value = std::array<std::uint8_t, 4>{1U, 2U, 3U, 4U};
   constexpr auto same = std::array<std::uint8_t, 4>{1U, 2U, 3U, 4U};
   constexpr auto different = std::array<std::uint8_t, 4>{1U, 2U, 0U, 4U};
   constexpr auto shorter = std::array<std::uint8_t, 3>{1U, 2U, 3U};

   BOOST_TEST(forge::crypto::core::constant_time_equal(value, same));
   BOOST_TEST(!forge::crypto::core::constant_time_equal(value, different));
   BOOST_TEST(!forge::crypto::core::constant_time_equal(value, shorter));
   BOOST_TEST(
       forge::crypto::core::constant_time_equal(std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{}));
}

BOOST_AUTO_TEST_CASE(secret_string_owns_copy_and_move_lifetimes) {
   static_assert(std::is_copy_constructible_v<forge::crypto::core::secret_string>);
   static_assert(std::is_move_constructible_v<forge::crypto::core::secret_string>);

   auto original = forge::crypto::core::secret_string{"private material"};
   auto copy = original;
   BOOST_TEST(copy.view() == "private material");

   auto moved = std::move(original);
   BOOST_TEST(original.empty());
   BOOST_TEST(moved.view() == "private material");

   copy = std::string{"replacement"};
   BOOST_TEST(copy.view() == "replacement");
   copy = std::move(moved);
   BOOST_TEST(moved.empty());
   BOOST_TEST(copy.view() == "private material");

   copy.clear();
   BOOST_TEST(copy.empty());
}

BOOST_AUTO_TEST_SUITE_END()
