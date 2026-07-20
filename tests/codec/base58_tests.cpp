#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import forge.codec.base58;

namespace {

[[nodiscard]] std::vector<std::uint8_t> bytes(std::string_view value) {
   return {value.begin(), value.end()};
}

[[nodiscard]] std::string text(std::span<const std::uint8_t> value) {
   return {value.begin(), value.end()};
}

} // namespace

BOOST_AUTO_TEST_SUITE(codec_base58_tests)

BOOST_AUTO_TEST_CASE(bitcoin_vectors_and_leading_zeroes_are_preserved) {
   const auto hello_world = bytes("hello world");
   BOOST_TEST(forge::codec::base58::encode(hello_world) == "StV1DL6CwTryKyV");
   BOOST_TEST(text(forge::codec::base58::decode("StV1DL6CwTryKyV")) == "hello world");

   const auto leading_zeroes = std::vector<std::uint8_t>{0, 0, 0, 1};
   BOOST_TEST(forge::codec::base58::encode(leading_zeroes) == "1112");
   BOOST_CHECK(forge::codec::base58::decode("1112") == leading_zeroes);

   const auto large = std::vector<std::uint8_t>(4096U, 0xa5U);
   BOOST_CHECK(forge::codec::base58::decode(forge::codec::base58::encode(large)) == large);
}

BOOST_AUTO_TEST_CASE(non_alphabet_input_is_rejected) {
   using invalid_input = forge::codec::base58::exceptions::invalid_input;
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base58::decode("0OIl")), invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base58::decode(" 2")), invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base58::decode("2\n")), invalid_input);
}

BOOST_AUTO_TEST_SUITE_END()
