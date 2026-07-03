#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import forge.compression.exceptions;
import forge.compression.zlib;

namespace compression = forge::compression;

namespace {

std::vector<char> bytes(std::string_view value) {
   return {value.begin(), value.end()};
}

} // namespace

BOOST_AUTO_TEST_SUITE(forge_compression_zlib)

BOOST_AUTO_TEST_CASE(zlib_round_trips_bytes) {
   const auto input = bytes("forge compression zlib round trip payload");

   const auto compressed = compression::zlib_compress(input, compression::zlib_level::best_compression);
   const auto decompressed = compression::zlib_decompress(compressed);

   BOOST_TEST(!compressed.empty());
   BOOST_TEST(decompressed == input);
}

BOOST_AUTO_TEST_CASE(zlib_round_trips_empty_input) {
   const auto input = std::vector<char>{};

   const auto compressed = compression::zlib_compress(input);
   const auto decompressed = compression::zlib_decompress(compressed);

   BOOST_TEST(!compressed.empty());
   BOOST_TEST(decompressed.empty());
}

BOOST_AUTO_TEST_CASE(zlib_rejects_invalid_input) {
   const auto invalid = bytes("not a zlib stream");

   BOOST_CHECK_THROW(
      compression::zlib_decompress(invalid),
      compression::exceptions::invalid_input
   );
}

BOOST_AUTO_TEST_CASE(zlib_enforces_output_limit) {
   auto input = std::vector<char>(4096, char{'x'});
   const auto compressed = compression::zlib_compress(input, compression::zlib_level::best_compression);

   BOOST_CHECK_THROW(
      compression::zlib_decompress(compressed, compression::zlib_limits{.max_output_size = 1024}),
      compression::exceptions::output_limit
   );
}

BOOST_AUTO_TEST_SUITE_END()
