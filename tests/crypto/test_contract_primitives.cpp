#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

import forge.crypto.bls.primitives;
import forge.codec.hex;

namespace {

std::vector<std::uint8_t> decode(std::string_view value) {
   auto result = std::vector<std::uint8_t>(value.size() / 2U);
   forge::codec::hex::decode(value, result);
   return result;
}

} // namespace

BOOST_AUTO_TEST_SUITE(contract_crypto_primitives)

BOOST_AUTO_TEST_CASE(bls_g1_add_matches_spring_vector) {
   const auto operand = decode("bbc622db0af03afbef1a7af93fe8556c58ac1b173f3a4ea105b974974f8c68c3"
                               "0faca94f8c63952694d79731a7d3f117e1e7c5462923aa0ce48a88a244c73cd0"
                               "edb3042ccb18db00f60ad0d595e0f5fce48a1d74ed309ea0f1a0aae381f4b308");
   const auto expected = decode("4e0fbf29558c9ac3427c1c8fbb758fe22aa658c30a2d90432501289130db21970"
                                "c45a950ebc8088846674d90eacb7205289d7479198886ba1bbd16cdd4d9564c6"
                                "ad75f1d02b93bf761e47086cb3eba22388e9d7773a6fd22a373c6ab8c9d6a16");
   auto result = std::vector<std::uint8_t>(96U);

   BOOST_TEST(forge::crypto::bls::primitives::g1_add(operand, operand, result) == 0);
   BOOST_TEST(result == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(bls_g1_map_matches_spring_vector) {
   const auto element = std::vector<std::uint8_t>(48U);
   const auto expected = decode("15a40e7d0c871905db43ac11172e2f59acbc93fbd6f29750faf2d5454cfa732a"
                                "37504ed19ade305c2d338f2b37a0a91133212273e76ea9ea9fdf2be2e8d42f39"
                                "63cd476abce81651448cf5556b92ff40e28d78a34bc2519f71a06441990f2c09");
   auto result = std::vector<std::uint8_t>(96U);

   BOOST_TEST(forge::crypto::bls::primitives::g1_map(element, result) == 0);
   BOOST_TEST(result == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(bls_primitives_reject_noncanonical_sizes) {
   auto g1 = std::vector<std::uint8_t>(96U);
   auto g2 = std::vector<std::uint8_t>(192U);
   auto scalar = std::vector<std::uint8_t>(32U);
   auto result = std::vector<std::uint8_t>(96U);

   g1.pop_back();
   BOOST_TEST(forge::crypto::bls::primitives::g1_add(g1, result, result) == -1);
   BOOST_TEST(forge::crypto::bls::primitives::g1_weighted_sum(g1, scalar, 1U, result) == -1);
   BOOST_TEST(forge::crypto::bls::primitives::pairing(g1, g2, 1U, result) == -1);
}

BOOST_AUTO_TEST_SUITE_END()
