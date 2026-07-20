#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import forge.codec.base64;

namespace {

[[nodiscard]] std::vector<std::uint8_t> bytes(std::string_view value) {
   return {value.begin(), value.end()};
}

[[nodiscard]] std::string text(std::span<const std::uint8_t> value) {
   return {value.begin(), value.end()};
}

} // namespace

BOOST_AUTO_TEST_SUITE(codec_base64_tests)

BOOST_AUTO_TEST_CASE(rfc4648_vectors) {
   BOOST_TEST(forge::codec::base64::encode("") == "");
   BOOST_TEST(forge::codec::base64::encode("f") == "Zg==");
   BOOST_TEST(forge::codec::base64::encode("fo") == "Zm8=");
   BOOST_TEST(forge::codec::base64::encode("foo") == "Zm9v");
   BOOST_TEST(forge::codec::base64::encode("foob") == "Zm9vYg==");
   BOOST_TEST(forge::codec::base64::encode("fooba") == "Zm9vYmE=");
   BOOST_TEST(forge::codec::base64::encode("foobar") == "Zm9vYmFy");
   BOOST_TEST(text(forge::codec::base64::decode("Zm9vYmFy")) == "foobar");
   BOOST_TEST(text(forge::codec::base64::decode("Zg==")) == "f");
   BOOST_TEST(text(forge::codec::base64::decode("Zg")) == "f");
}

BOOST_AUTO_TEST_CASE(url_padding_and_wrapping_are_explicit) {
   const auto input = bytes("abc123$&()'?\xb4\xf5\x01\xfa~a");
   const auto url_options = forge::codec::base64::encode_options{
       .characters = forge::codec::base64::alphabet::url,
       .pad = forge::codec::base64::padding::omit,
   };
   BOOST_TEST(forge::codec::base64::encode(input, url_options) == "YWJjMTIzJCYoKSc_tPUB-n5h");
   BOOST_TEST(text(forge::codec::base64::decode("YWJjMTIzJCYoKSc_tPUB-n5h",
                                                {.characters = forge::codec::base64::alphabet::url,
                                                 .pad = forge::codec::base64::padding_policy::allow})) == text(input));
   BOOST_TEST(forge::codec::base64::encode("abcdef", {.line_width = 4}) == "YWJj\nZGVm");
   BOOST_TEST(text(forge::codec::base64::decode("YWJj\r\nZGVm", {.ignore_ascii_whitespace = true})) == "abcdef");
}

BOOST_AUTO_TEST_CASE(noncanonical_input_is_rejected) {
   using invalid_input = forge::codec::base64::exceptions::invalid_input;
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base64::decode("YQ==evil")), invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base64::decode("Y=Q=")), invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base64::decode("YQ===")), invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base64::decode("Y")), invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base64::decode("AB==")), invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base64::decode("AAB=")), invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base64::decode("YW Jj")), invalid_input);
   BOOST_CHECK_THROW(static_cast<void>(forge::codec::base64::decode("AA-_")), invalid_input);
   BOOST_CHECK_THROW(
       static_cast<void>(forge::codec::base64::decode("AA+/", {.characters = forge::codec::base64::alphabet::url})),
       invalid_input);
   BOOST_CHECK_THROW(
       static_cast<void>(forge::codec::base64::decode("YQ", {.pad = forge::codec::base64::padding_policy::require})),
       invalid_input);
   BOOST_CHECK_THROW(
       static_cast<void>(forge::codec::base64::decode("YQ==", {.pad = forge::codec::base64::padding_policy::forbid})),
       invalid_input);
}

BOOST_AUTO_TEST_SUITE_END()
