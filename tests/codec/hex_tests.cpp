#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

import forge.codec.hex;

BOOST_AUTO_TEST_SUITE(codec_hex_tests)

BOOST_AUTO_TEST_CASE(case_integer_width_and_strict_decode_are_supported) {
   const auto input = std::array<std::uint8_t, 3>{0x00U, 0xa5U, 0xffU};
   BOOST_TEST(forge::codec::hex::encode(input) == "00a5ff");
   BOOST_TEST(forge::codec::hex::encode(input, forge::codec::hex::letter_case::upper) == "00A5FF");
   BOOST_TEST(forge::codec::hex::encode(std::uint32_t{0x12abU}, 8U) == "000012ab");
   BOOST_CHECK(forge::codec::hex::decode("00A5ff") == std::vector<std::uint8_t>(input.begin(), input.end()));

   auto output = std::array<std::uint8_t, 2>{};
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::hex::decode("abc", output)),
                     forge::codec::hex::exceptions::invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::hex::decode("zz", output)),
                     forge::codec::hex::exceptions::invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::hex::decode("001122", output)),
                     forge::codec::hex::exceptions::insufficient_output);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::hex::decode("abc")), forge::codec::hex::exceptions::invalid_input);
   BOOST_CHECK_THROW(
       static_cast<void>(forge::codec::hex::encode(std::uint8_t{1}, std::numeric_limits<std::size_t>::max())),
       forge::codec::hex::exceptions::invalid_input);
}

BOOST_AUTO_TEST_SUITE_END()
