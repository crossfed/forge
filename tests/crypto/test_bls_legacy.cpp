#include <boost/test/unit_test.hpp>
#include <forge/exceptions/macros.hpp>

#include <array>
#include <concepts>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

import forge.codec.hex;
import forge.codec.json;
import forge.crypto.bls;
import forge.crypto.digest.sha256;
import forge.exceptions;
import forge.raw.exceptions;
import forge.raw.raw;
import forge.variant.chrono;
import forge.variant.containers;
import forge.variant.conversion;
import forge.variant.described;
import forge.variant.exceptions;
import forge.variant.format;
import forge.variant.multiprecision;
import forge.variant.value;

using namespace forge::crypto::bls;

namespace {

const auto seed_1 = std::vector<std::uint8_t>{0,   50, 6,   244, 24,  199, 1,  25, 52, 88,  192, 19, 18, 12, 89,  6,
                                              220, 18, 102, 58,  209, 82,  12, 62, 89, 110, 182, 9,  44, 20, 254, 22};
const auto seed_2 = std::vector<std::uint8_t>{6,  51, 22, 89, 11, 15,  4,   61, 127, 241, 79,  26, 88, 52, 1,   6,
                                              18, 79, 10, 8,  36, 182, 154, 35, 75,  156, 215, 41, 29, 90, 125, 233};

const auto message_1 = std::vector<std::uint8_t>{51, 23, 56, 93, 212, 129, 128, 27, 251, 12, 42, 129, 210, 9, 34, 98};
const auto message_2 = std::vector<std::uint8_t>{16, 38, 54, 125, 71, 214, 217, 78, 73, 23, 127, 235, 8, 94, 41, 53};

constexpr auto private_text = "PVT_BLS_vh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc";
constexpr auto public_text =
    "PUB_BLS_82P3oM1u0IEv64u9i4vSzvg1-"
    "QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg";
constexpr auto signature_text =
    "SIG_BLS_RrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_"
    "Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-"
    "9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg";

template <typename Value>
concept has_to_string = requires(const Value& value) { value.to_string(); };

template <typename Value>
concept has_aggregate = requires(Value& value, const signature& item) { value.aggregate(item); };

template <typename Value>
concept has_rvalue_serialize = requires(Value&& value) { std::move(value).serialize(); };

static_assert(sizeof(public_key) == public_key::size_bytes);
static_assert(sizeof(signature) == signature::size_bytes);
static_assert(sizeof(aggregate_signature) == aggregate_signature::size_bytes);
static_assert(std::same_as<decltype(std::declval<const public_key&>().bytes()),
                           std::span<const std::uint8_t, public_key::size_bytes>>);
static_assert(std::same_as<decltype(std::declval<const signature&>().bytes()),
                           std::span<const std::uint8_t, signature::size_bytes>>);
static_assert(!std::is_constructible_v<public_key, std::string>);
static_assert(!std::is_constructible_v<signature, std::string>);
static_assert(!std::is_constructible_v<aggregate_signature, std::string>);
static_assert(!has_to_string<public_key>);
static_assert(!has_to_string<signature>);
static_assert(!has_to_string<aggregate_signature>);
static_assert(!has_aggregate<aggregate_signature>);
static_assert(!has_rvalue_serialize<public_key>);
static_assert(!has_rvalue_serialize<signature>);
static_assert(!has_rvalue_serialize<aggregate_signature>);
static_assert(!std::is_constructible_v<proof_verified_public_key, public_key>);

} // namespace

BOOST_AUTO_TEST_SUITE(bls_test)

BOOST_AUTO_TEST_CASE(bls_value_raw_encoding) try {
   const auto secret = private_key{seed_1};
   const auto key = secret.get_public_key();
   const auto value = secret.sign(message_1);
   auto accumulator = signature_accumulator{};
   accumulator.add(value);
   const auto aggregate = accumulator.finish();

   BOOST_TEST(key.serialize().size() == public_key::size_bytes);
   BOOST_TEST(value.serialize().size() == signature::size_bytes);
   BOOST_TEST(aggregate.serialize().size() == aggregate_signature::size_bytes);

   const auto key_wire = forge::raw::pack(key);
   const auto signature_wire = forge::raw::pack(value);
   const auto aggregate_wire = forge::raw::pack(aggregate);
   BOOST_REQUIRE_EQUAL(key_wire.size(), public_key::size_bytes + 1U);
   BOOST_REQUIRE_EQUAL(signature_wire.size(), signature::size_bytes + 2U);
   BOOST_REQUIRE_EQUAL(aggregate_wire.size(), aggregate_signature::size_bytes + 2U);
   BOOST_TEST(key_wire[0] == public_key::size_bytes);
   BOOST_TEST(signature_wire[0] == 0xc0U);
   BOOST_TEST(signature_wire[1] == 0x01U);
   BOOST_TEST(static_cast<bool>(forge::raw::unpack<public_key>(key_wire) == key));
   BOOST_TEST(static_cast<bool>(forge::raw::unpack<signature>(signature_wire) == value));
   BOOST_TEST(static_cast<bool>(forge::raw::unpack<aggregate_signature>(aggregate_wire) == aggregate));

   auto wrong_key_length = key_wire;
   wrong_key_length[0] = 95U;
   BOOST_CHECK_THROW(forge::raw::unpack<public_key>(wrong_key_length), forge::raw::exceptions::codec_error);

   auto wrong_signature_length = signature_wire;
   wrong_signature_length[0] = 0xbfU;
   BOOST_CHECK_THROW(forge::raw::unpack<signature>(wrong_signature_length), forge::raw::exceptions::codec_error);

   auto wrong_aggregate_length = aggregate_wire;
   wrong_aggregate_length[0] = 0xc1U;
   BOOST_CHECK_THROW(forge::raw::unpack<aggregate_signature>(wrong_aggregate_length),
                     forge::raw::exceptions::codec_error);

   auto truncated_key = key_wire;
   auto truncated_signature = signature_wire;
   auto truncated_aggregate = aggregate_wire;
   truncated_key.pop_back();
   truncated_signature.pop_back();
   truncated_aggregate.pop_back();
   BOOST_CHECK_THROW(forge::raw::unpack<public_key>(truncated_key), forge::raw::exceptions::range_error);
   BOOST_CHECK_THROW(forge::raw::unpack<signature>(truncated_signature), forge::raw::exceptions::range_error);
   BOOST_CHECK_THROW(forge::raw::unpack<aggregate_signature>(truncated_aggregate), forge::raw::exceptions::range_error);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_sign_verify) try {
   const auto secret = private_key{seed_1};
   const auto key = secret.get_public_key();
   const auto value = secret.sign(message_1);

   BOOST_TEST(valid(key));
   BOOST_TEST(valid(value));
   BOOST_TEST(verify(key, message_1, value));

   auto tampered = message_1;
   tampered.front() ^= 0x01U;
   BOOST_TEST(!verify(key, tampered, value));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_sign_digest) try {
   const auto secret = private_key{seed_1};
   const auto key = secret.get_public_key();
   const auto digest = forge::crypto::digest::sha256::hash(std::string{"BLS digest message"});
   const auto message =
       std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(digest.data()), digest.data_size()};
   const auto value = secret.sign(message);

   BOOST_TEST(verify(key, message, value));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_grouped_aggregate_verification) try {
   const auto secret_1 = private_key{seed_1};
   const auto secret_2 = private_key{seed_2};
   const auto key_1 = secret_1.get_public_key();
   const auto key_2 = secret_2.get_public_key();
   const auto verified_1 = verify_proof_of_possession(key_1, secret_1.proof_of_possession());
   const auto verified_2 = verify_proof_of_possession(key_2, secret_2.proof_of_possession());
   BOOST_REQUIRE(verified_1);
   BOOST_REQUIRE(verified_2);
   BOOST_TEST(verify(*verified_1, message_1, secret_1.sign(message_1)));
   BOOST_TEST(!verify(*verified_1, message_2, secret_1.sign(message_1)));
   BOOST_TEST(!verify_proof_of_possession(key_2, secret_1.proof_of_possession()));

   const auto strong_keys = std::array{*verified_1, *verified_2};
   const auto weak_keys = std::array{*verified_1};

   auto first = signature_accumulator{};
   first.add(secret_1.sign(message_1));
   first.add(secret_2.sign(message_1));
   const auto same_message = first.finish();

   auto combined = signature_accumulator{};
   combined.add(same_message);
   combined.add(secret_1.sign(message_2));
   const auto aggregate = combined.finish();
   BOOST_TEST(valid(aggregate));

   const auto groups = std::array{
       aggregate_verification_group{.public_keys = strong_keys, .message = message_1},
       aggregate_verification_group{.public_keys = weak_keys, .message = message_2},
   };
   BOOST_TEST(verify_grouped(groups, aggregate));

   auto tampered = message_2;
   tampered.front() ^= 0x01U;
   const auto invalid_groups = std::array{
       aggregate_verification_group{.public_keys = strong_keys, .message = message_1},
       aggregate_verification_group{.public_keys = weak_keys, .message = tampered},
   };
   BOOST_TEST(!verify_grouped(invalid_groups, aggregate));
   BOOST_TEST(!verify_grouped({}, aggregate));
   BOOST_TEST(
       !verify_grouped(std::array{aggregate_verification_group{.public_keys = {}, .message = message_1}}, aggregate));

   auto duplicate = signature_accumulator{};
   duplicate.add(secret_1.sign(message_1));
   duplicate.add(secret_2.sign(message_1));
   duplicate.add(secret_1.sign(message_1));
   BOOST_TEST(!verify_grouped(
       std::array{
           aggregate_verification_group{.public_keys = strong_keys, .message = message_1},
           aggregate_verification_group{.public_keys = weak_keys, .message = message_1},
       },
       duplicate.finish()));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_cached_key_and_accumulator_value_semantics) try {
   const auto secret = private_key{seed_1};
   const auto key = secret.get_public_key();
   const auto value = secret.sign(message_1);
   const auto verified = verify_proof_of_possession(key, secret.proof_of_possession());
   BOOST_REQUIRE(verified);

   auto copied_key = *verified;
   auto moved_key = std::move(copied_key);
   BOOST_TEST(verify(*verified, message_1, value));
   BOOST_TEST(verify(moved_key, message_1, value));
   BOOST_TEST(!verify(copied_key, message_1, value));

   auto accumulator = signature_accumulator{};
   accumulator.add(value);
   auto copied_accumulator = accumulator;
   copied_accumulator.add(value);
   BOOST_TEST(static_cast<bool>(accumulator.finish() != copied_accumulator.finish()));

   auto moved_accumulator = std::move(copied_accumulator);
   BOOST_TEST(valid(moved_accumulator.finish()));
   BOOST_CHECK_THROW(copied_accumulator.add(value), exceptions::invalid_accumulator);
   BOOST_CHECK_THROW((void)copied_accumulator.finish(), exceptions::invalid_accumulator);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_malformed_values_are_non_throwing) try {
   const auto malformed_key = public_key{};
   const auto malformed_signature = signature{};
   const auto malformed_aggregate = aggregate_signature{};

   BOOST_TEST(!valid(malformed_key));
   BOOST_TEST(!valid(malformed_signature));
   BOOST_TEST(!valid(malformed_aggregate));
   BOOST_TEST(!verify(malformed_key, message_1, malformed_signature));
   BOOST_TEST(!verify_proof_of_possession(malformed_key, malformed_signature));
   BOOST_TEST(!verify_grouped({}, malformed_aggregate));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_rejects_points_outside_the_correct_subgroup) try {
   constexpr auto key_hex =
       "9eb987464f483a62537c33715426bd5fd50c7f5e85f51c634d85081974df95794fc79e95ee5aafa38578ad42f502b1124"
       "ddf24f4172a370ce94e3cc81eaa698b9bf4762d4a31f01015b179eae0ee37aa6b07a8edc1246defae68c4f7139bb40d";
   constexpr auto signature_hex =
       "274bca7d08d71ed1ad63d37af4a90e47c9b15f54d29667fe6b4287b25902f68ba6df1eba4bdb439460efba65c7b8e103"
       "c65fa1def3b431f2c0dbb3ff37d4a8cd6e778f1581f355e56ce1bb9acd8d04f13d4da481540e8c35a2b4319ab59b1418"
       "a1535c7c6306445d855e7233c4c2fea99afc6423304847d728363c0a55c05207eb14d5d481b23967aacbe94ed055a11604"
       "00d2d261972d340755130e5d8cd54fba4006433750957b240d2b51d882fafa043cc219bd846c84dc3c3c43f9a8e90b";

   auto key_bytes = public_key::data_type{};
   auto signature_bytes = signature::data_type{};
   BOOST_REQUIRE_EQUAL(forge::codec::hex::decode(key_hex, key_bytes), key_bytes.size());
   BOOST_REQUIRE_EQUAL(forge::codec::hex::decode(signature_hex, signature_bytes), signature_bytes.size());
   const auto key = public_key{key_bytes};
   const auto value = signature{signature_bytes};
   const auto aggregate = aggregate_signature{signature_bytes};

   BOOST_TEST(!valid(key));
   BOOST_TEST(!valid(value));
   BOOST_TEST(!valid(aggregate));
   BOOST_TEST(!verify(key, message_1, value));
   BOOST_CHECK_THROW((void)encoding::parse_public_key(encoding::format(key)), exceptions::parse_error);
   BOOST_CHECK_THROW((void)encoding::parse_signature(encoding::format(value)), exceptions::parse_error);
   BOOST_CHECK_THROW((void)encoding::parse_aggregate_signature(encoding::format(aggregate)), exceptions::parse_error);

   auto accumulator = signature_accumulator{};
   BOOST_CHECK_THROW(accumulator.add(value), exceptions::invalid_signature);
   BOOST_CHECK_THROW(accumulator.add(aggregate), exceptions::invalid_signature);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_accumulator_rejects_identity_result) try {
   constexpr auto inverse_hex =
       "75814691ce0201c19cd7d5cafcf58c5d6451cdd7b9995d5173fce9561be16f47b15fcec7e73e0e194e5f1050b9791f14"
       "3804f65d46b0b60ef08f1a7eadf680193aa133cec2ef4190f0d3ec69881be17f9198c0d05cae4cfa804752fccd017d0a"
       "3d091bd24f3f485562964f1f83427c67a199e53463d07c70920f2f384478f3d3e096d88086b11b71832f949a57075412"
       "413b3e3962dcce95b9b4a274eda6d7e05133a65e27d92809396b61e88b27bef6ee41a13d7940cf4acb6276a83882c808";
   auto inverse_bytes = signature::data_type{};
   BOOST_REQUIRE_EQUAL(forge::codec::hex::decode(inverse_hex, inverse_bytes), inverse_bytes.size());
   const auto inverse = signature{inverse_bytes};
   BOOST_REQUIRE(valid(inverse));

   auto accumulator = signature_accumulator{};
   accumulator.add(private_key{seed_1}.sign(message_1));
   accumulator.add(inverse);
   BOOST_CHECK_THROW((void)accumulator.finish(), exceptions::invalid_accumulator);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_typed_misuse_errors) try {
   auto accumulator = signature_accumulator{};
   BOOST_CHECK_THROW((void)accumulator.finish(), exceptions::invalid_accumulator);
   BOOST_CHECK_THROW(accumulator.add(signature{}), exceptions::invalid_signature);
   BOOST_CHECK_THROW(accumulator.add(aggregate_signature{}), exceptions::invalid_signature);
   BOOST_CHECK_THROW(private_key(std::span<const std::uint8_t>{seed_1.data(), 8U}), exceptions::invalid_private_key);

   const auto malformed_text = encoding::format(public_key{});
   BOOST_CHECK_THROW((void)encoding::parse_public_key(malformed_text), exceptions::parse_error);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_encoding_roundtrip_and_legacy_vectors) try {
   const auto secret = encoding::parse_private_key(private_text);
   const auto key = encoding::parse_public_key(public_text);
   const auto value = encoding::parse_signature(signature_text);
   const auto aggregate = encoding::parse_aggregate_signature(signature_text);

   BOOST_TEST(encoding::format(secret) == private_text);
   BOOST_TEST(encoding::format(key) == public_text);
   BOOST_TEST(encoding::format(value) == signature_text);
   BOOST_TEST(encoding::format(aggregate) == signature_text);
   BOOST_TEST(valid(key));
   BOOST_TEST(valid(value));
   BOOST_TEST(valid(aggregate));

   const auto regenerated = private_key{seed_1};
   const auto regenerated_text = encoding::format(regenerated);
   const auto regenerated_public_text = encoding::format(regenerated.get_public_key());
   BOOST_TEST(static_cast<bool>(encoding::parse_private_key(regenerated_text) == regenerated));
   BOOST_TEST(static_cast<bool>(encoding::parse_public_key(regenerated_public_text) == regenerated.get_public_key()));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_encoding_rejects_invalid_text) try {
   BOOST_CHECK_THROW((void)encoding::parse_private_key("x"), exceptions::parse_error);
   BOOST_CHECK_THROW((void)encoding::parse_public_key("x"), exceptions::parse_error);
   BOOST_CHECK_THROW((void)encoding::parse_signature("x"), exceptions::parse_error);
   BOOST_CHECK_THROW((void)encoding::parse_aggregate_signature("x"), exceptions::parse_error);

   BOOST_CHECK_THROW((void)encoding::parse_private_key("PVT_BLS_wh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc"),
                     exceptions::parse_error);
   BOOST_CHECK_THROW(
       (void)encoding::parse_public_key("PUB_BLS_92P3oM1u0IEv64u9i4vSzvg1-"
                                        "QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJ"
                                        "Y7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg"),
       exceptions::parse_error);
   BOOST_CHECK_THROW((void)encoding::parse_signature(
                         "SIG_BLS_SrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_"
                         "Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-"
                         "9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg"),
                     exceptions::parse_error);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_variant_uses_encoding) try {
   const auto secret = encoding::parse_private_key(private_text);
   const auto key = encoding::parse_public_key(public_text);
   const auto value = encoding::parse_signature(signature_text);
   const auto aggregate = encoding::parse_aggregate_signature(signature_text);

   auto encoded = forge::variant{secret};
   BOOST_TEST(forge::codec::json::write_value(encoded).text == '"' + std::string{private_text} + '"');
   encoded = key;
   BOOST_TEST(forge::codec::json::write_value(encoded).text == '"' + std::string{public_text} + '"');
   encoded = value;
   BOOST_TEST(forge::codec::json::write_value(encoded).text == '"' + std::string{signature_text} + '"');
   encoded = aggregate;
   BOOST_TEST(forge::codec::json::write_value(encoded).text == '"' + std::string{signature_text} + '"');
   BOOST_TEST(static_cast<bool>(encoded.as<aggregate_signature>() == aggregate));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_random_key_generation) try {
   const auto secret = private_key::generate();
   const auto key = secret.get_public_key();
   const auto value = secret.sign(message_1);
   BOOST_TEST(verify(key, message_1, value));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
