#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

import forge.codec.hex;
import forge.crypto.bn256;

namespace {

std::vector<std::uint8_t> decode(std::string_view value) {
   auto result = std::vector<std::uint8_t>(value.size() / 2U);
   forge::codec::hex::decode(value, result);
   return result;
}

} // namespace

BOOST_AUTO_TEST_SUITE(crypto_bn256)

BOOST_AUTO_TEST_CASE(add_matches_spring_vector) {
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

BOOST_AUTO_TEST_CASE(multiply_matches_spring_vector) {
   const auto point = decode("007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f9136"
                             "0db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be");
   const auto scalar = decode("0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b");
   const auto expected = decode("2d66cdeca5e1715896a5a924c50a149be87ddd2347b862150fbb0fd7d0b1833c"
                                "11c76319ebefc5379f7aa6d85d40169a612597637242a4bbb39e5cd3b844becd");
   auto result = std::vector<std::uint8_t>(64U);

   BOOST_TEST(forge::crypto::bn256::multiply(point, scalar, result) == 0);
   BOOST_TEST(result == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(rejects_noncanonical_operand_sizes) {
   auto point = std::vector<std::uint8_t>(64U);
   auto scalar = std::vector<std::uint8_t>(32U);
   auto result = std::vector<std::uint8_t>(64U);

   point.pop_back();
   BOOST_TEST(forge::crypto::bn256::add(point, result, result) == -1);
   BOOST_TEST(forge::crypto::bn256::multiply(point, scalar, result) == -1);
   BOOST_TEST(forge::crypto::bn256::pairing_check(point) == -1);
}

BOOST_AUTO_TEST_SUITE_END()
