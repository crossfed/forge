#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <forge/exceptions/macros.hpp>

import forge.codec.base32;
import forge.exceptions;

BOOST_AUTO_TEST_SUITE(base32)

BOOST_AUTO_TEST_CASE(base32_rfc4648_vectors_without_padding) try {
   const auto check = [](std::string input, std::string expected) {
      const auto bytes = std::vector<std::uint8_t>(input.begin(), input.end());
      BOOST_CHECK_EQUAL(forge::codec::base32::encode(bytes), expected);

      auto decoded = forge::codec::base32::decode(expected);
      BOOST_CHECK_EQUAL(std::string(decoded.begin(), decoded.end()), input);
   };

   check("", "");
   check("f", "my");
   check("fo", "mzxq");
   check("foo", "mzxw6");
   check("foob", "mzxw6yq");
   check("fooba", "mzxw6ytb");
   check("foobar", "mzxw6ytboi");
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base32_decode_accepts_uppercase_and_padding) try {
   auto decoded = forge::codec::base32::decode("MZXW6YTBOI======");
   BOOST_CHECK_EQUAL(std::string(decoded.begin(), decoded.end()), "foobar");
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base32_rejects_invalid_characters) try {
   BOOST_CHECK_EXCEPTION((void)forge::codec::base32::decode("mzxw6ytb0i"),
                         forge::codec::base32::exceptions::invalid_options,
                         [](const forge::codec::base32::exceptions::invalid_options& error) {
      return error.code().category().name() == std::string_view{"forge.codec.base32"};
   });
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
