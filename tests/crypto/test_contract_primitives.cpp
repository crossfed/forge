#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

import forge.crypto.bls.primitives;
import forge.crypto.bn256;
import forge.crypto.hex;

namespace {

std::vector<std::uint8_t> decode(std::string_view value) {
   auto result = std::vector<std::uint8_t>(value.size() / 2U);
   forge::crypto::from_hex(std::string{value}, result.data(), result.size());
   return result;
}

} // namespace

BOOST_AUTO_TEST_SUITE(contract_crypto_primitives)

BOOST_AUTO_TEST_CASE(bn256_add_matches_spring_vector) {
   const auto left = decode("222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c09"
                            "2976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2");
   const auto right = decode("1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f533"
                             "22a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844");
   const auto expected = decode("16c7c4042e3a725ddbacf197c519c3dcad2bc87dfd9ac7e1e1631154ee0b7d9c"
                                "19cd640dd28c9811ebaaa095a16b16190d08d6906c4f926fce581985fe35be0e");
   auto result = std::vector<std::uint8_t>(64U);

   BOOST_TEST(forge::crypto::bn256::add(left, right, result) == 0);
   BOOST_TEST(result == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(bn256_multiply_matches_spring_vector) {
   const auto point = decode("007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f9136"
                             "0db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be");
   const auto scalar = decode("0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b");
   const auto expected = decode("2d66cdeca5e1715896a5a924c50a149be87ddd2347b862150fbb0fd7d0b1833c"
                                "11c76319ebefc5379f7aa6d85d40169a612597637242a4bbb39e5cd3b844becd");
   auto result = std::vector<std::uint8_t>(64U);

   BOOST_TEST(forge::crypto::bn256::multiply(point, scalar, result) == 0);
   BOOST_TEST(result == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(bn256_rejects_noncanonical_operand_sizes) {
   auto point = std::vector<std::uint8_t>(64U);
   auto scalar = std::vector<std::uint8_t>(32U);
   auto result = std::vector<std::uint8_t>(64U);

   point.pop_back();
   BOOST_TEST(forge::crypto::bn256::add(point, result, result) == -1);
   BOOST_TEST(forge::crypto::bn256::multiply(point, scalar, result) == -1);
   BOOST_TEST(forge::crypto::bn256::pairing_check(std::span<const std::uint8_t>{point}) == -1);
}

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
