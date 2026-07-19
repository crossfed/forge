module;

#include <forge/contract/internal/intrinsics.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module forge.contract.crypto_bls_ext;

export import forge.contract.base64;
export import forge.contract.crypto;
export import forge.crypto.bls.values;

import forge.contract.intrinsics;

export namespace forge::contract {

using bls_scalar = forge::crypto::bls::scalar;
using bls_fp = forge::crypto::bls::field_element;
using bls_s = forge::crypto::bls::wide_scalar;
using bls_fp2 = forge::crypto::bls::field_element2;
using bls_g1 = forge::crypto::bls::g1;
using bls_g2 = forge::crypto::bls::g2;
using bls_gt = forge::crypto::bls::gt;

[[nodiscard]] inline std::int32_t bls_g1_add(const bls_g1& first, const bls_g1& second, bls_g1& result) {
   return ::forge::contract::internal::bls_g1_add(first.data(), first.size(), second.data(), second.size(),
                                                  result.data(), result.size());
}

[[nodiscard]] inline std::int32_t bls_g2_add(const bls_g2& first, const bls_g2& second, bls_g2& result) {
   return ::forge::contract::internal::bls_g2_add(first.data(), first.size(), second.data(), second.size(),
                                                  result.data(), result.size());
}

[[nodiscard]] inline std::int32_t bls_g1_weighted_sum(const bls_g1 points[], const bls_scalar scalars[],
                                                      std::uint32_t count, bls_g1& result) {
   if (count == 0U) {
      return failure;
   }
   return ::forge::contract::internal::bls_g1_weighted_sum(
       reinterpret_cast<const char*>(points), count * sizeof(bls_g1), reinterpret_cast<const char*>(scalars),
       count * sizeof(bls_scalar), count, result.data(), result.size());
}

[[nodiscard]] inline std::int32_t bls_g2_weighted_sum(const bls_g2 points[], const bls_scalar scalars[],
                                                      std::uint32_t count, bls_g2& result) {
   if (count == 0U) {
      return failure;
   }
   return ::forge::contract::internal::bls_g2_weighted_sum(
       reinterpret_cast<const char*>(points), count * sizeof(bls_g2), reinterpret_cast<const char*>(scalars),
       count * sizeof(bls_scalar), count, result.data(), result.size());
}

[[nodiscard]] inline std::int32_t bls_pairing(const bls_g1 first[], const bls_g2 second[], std::uint32_t count,
                                              bls_gt& result) {
   if (count == 0U) {
      return failure;
   }
   return ::forge::contract::internal::bls_pairing(reinterpret_cast<const char*>(first), count * sizeof(bls_g1),
                                                   reinterpret_cast<const char*>(second), count * sizeof(bls_g2), count,
                                                   result.data(), result.size());
}

[[nodiscard]] inline std::int32_t bls_g1_map(const bls_fp& value, bls_g1& result) {
   return ::forge::contract::internal::bls_g1_map(value.data(), value.size(), result.data(), result.size());
}

[[nodiscard]] inline std::int32_t bls_g2_map(const bls_fp2& value, bls_g2& result) {
   return ::forge::contract::internal::bls_g2_map(reinterpret_cast<const char*>(value.data()), sizeof(value),
                                                  result.data(), result.size());
}

[[nodiscard]] inline std::int32_t bls_fp_mod(const bls_s& value, bls_fp& result) {
   return ::forge::contract::internal::bls_fp_mod(value.data(), value.size(), result.data(), result.size());
}

[[nodiscard]] inline std::int32_t bls_fp_mul(const bls_fp& first, const bls_fp& second, bls_fp& result) {
   return ::forge::contract::internal::bls_fp_mul(first.data(), first.size(), second.data(), second.size(),
                                                  result.data(), result.size());
}

[[nodiscard]] inline std::int32_t bls_fp_exp(const bls_fp& base, const bls_s& exponent, bls_fp& result) {
   return ::forge::contract::internal::bls_fp_exp(base.data(), base.size(), exponent.data(), exponent.size(),
                                                  result.data(), result.size());
}

namespace detail {

inline constexpr auto bls_public_key_prefix = std::string_view{"PUB_BLS_"};
inline constexpr auto bls_signature_prefix = std::string_view{"SIG_BLS_"};
inline constexpr auto bls_checksum_size = std::size_t{4};
inline constexpr auto bls_signature_ciphersuite = std::string_view{"BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_"};
inline constexpr auto bls_pop_ciphersuite = std::string_view{"BLS_POP_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_"};

inline constexpr auto g1_one_negative = std::array<unsigned char, 96>{
    0xbb, 0xc6, 0x22, 0xdb, 0x0a, 0xf0, 0x3a, 0xfb, 0xef, 0x1a, 0x7a, 0xf9, 0x3f, 0xe8, 0x55, 0x6c,
    0x58, 0xac, 0x1b, 0x17, 0x3f, 0x3a, 0x4e, 0xa1, 0x05, 0xb9, 0x74, 0x97, 0x4f, 0x8c, 0x68, 0xc3,
    0x0f, 0xac, 0xa9, 0x4f, 0x8c, 0x63, 0x95, 0x26, 0x94, 0xd7, 0x97, 0x31, 0xa7, 0xd3, 0xf1, 0x17,
    0xca, 0xc2, 0x39, 0xb9, 0xd6, 0xdc, 0x54, 0xad, 0x1b, 0x75, 0xcb, 0x0e, 0xba, 0x38, 0x6f, 0x4e,
    0x36, 0x42, 0xac, 0xca, 0xd5, 0xb9, 0x55, 0x66, 0xc9, 0x07, 0xb5, 0x1d, 0xef, 0x6a, 0x81, 0x67,
    0xf2, 0x21, 0x2e, 0xcf, 0xc8, 0x76, 0x7d, 0xaa, 0xa8, 0x45, 0xd5, 0x55, 0x68, 0x1d, 0x4d, 0x11,
};

inline constexpr auto gt_one = [] {
   auto value = std::array<unsigned char, sizeof(bls_gt)>{};
   value[0] = 1U;
   return value;
}();

template <typename T> [[nodiscard]] std::string encode_bls_value(const T& value, std::string_view prefix) {
   auto bytes = std::array<char, sizeof(T) + bls_checksum_size>{};
   std::ranges::copy(value, bytes.begin());
   const auto checksum = ripemd160(value.data(), value.size()).extract_as_byte_array();
   std::copy_n(reinterpret_cast<const char*>(checksum.data()), bls_checksum_size, bytes.begin() + sizeof(T));
   return std::string{prefix} + base64url_encode(std::string_view{bytes.data(), bytes.size()});
}

template <typename T> [[nodiscard]] T decode_bls_value(std::string_view encoded, std::string_view prefix) {
   check(encoded.size() > prefix.size(), "encoded base64 key is too short");
   check(encoded.starts_with(prefix), "base64 encoded type must begin from corresponding prefix");
   const auto decoded = base64url_decode(encoded.substr(prefix.size()));
   check(decoded.size() == sizeof(T) + bls_checksum_size,
         "decoded size " + std::to_string(decoded.size()) + " doesn't match structure size " +
             std::to_string(sizeof(T)) + " + checksum " + std::to_string(bls_checksum_size));

   auto result = T{};
   std::copy_n(decoded.data(), sizeof(T), result.begin());
   const auto checksum = ripemd160(result.data(), result.size()).extract_as_byte_array();
   check(std::memcmp(decoded.data() + sizeof(T), checksum.data(), bls_checksum_size) == 0,
         "checksum of structure doesn't match");
   return result;
}

[[nodiscard]] inline std::array<std::uint8_t, checksum256::byte_size> sha256_bytes(std::span<const char> input) {
   return sha256(input.data(), static_cast<std::uint32_t>(input.size())).extract_as_byte_array();
}

inline void xmd_sha256(std::span<char> output, std::span<const char> input, std::string_view domain) {
   constexpr auto hash_size = std::size_t{32};
   constexpr auto block_size = std::size_t{64};
   const auto blocks = (output.size() + hash_size - 1U) / hash_size;
   check(blocks <= 255U && domain.size() <= 255U && output.size() <= 0xffffU, "invalid BLS XMD expansion size");

   auto initial = std::vector<char>(block_size, 0);
   initial.insert(initial.end(), input.begin(), input.end());
   initial.push_back(static_cast<char>((output.size() >> 8U) & 0xffU));
   initial.push_back(static_cast<char>(output.size() & 0xffU));
   initial.push_back(0);
   initial.insert(initial.end(), domain.begin(), domain.end());
   initial.push_back(static_cast<char>(domain.size()));
   const auto b0 = sha256_bytes(initial);

   auto previous = std::array<std::uint8_t, hash_size>{};
   for (auto block = std::size_t{1}; block <= blocks; ++block) {
      auto material = std::vector<char>{};
      material.reserve(hash_size + 1U + domain.size() + 1U);
      for (auto index = std::size_t{}; index < hash_size; ++index) {
         material.push_back(static_cast<char>(b0[index] ^ previous[index]));
      }
      material.push_back(static_cast<char>(block));
      material.insert(material.end(), domain.begin(), domain.end());
      material.push_back(static_cast<char>(domain.size()));
      previous = sha256_bytes(material);

      const auto offset = (block - 1U) * hash_size;
      const auto count = std::min(hash_size, output.size() - offset);
      std::copy_n(reinterpret_cast<const char*>(previous.data()), count, output.begin() + offset);
   }
}

[[nodiscard]] inline bls_s scalar_from_big_endian(const bls_s& input) {
   auto result = bls_s{};
   std::ranges::reverse_copy(input, result.begin());
   return result;
}

inline void g2_from_message(std::span<const char> message, std::string_view domain, bls_g2& result) {
   auto expanded = std::array<bls_s, 4>{};
   xmd_sha256(std::span<char>{expanded.front().data(), sizeof(expanded)}, message, domain);

   auto field = bls_fp2{};
   auto first = bls_g2{};
   auto second = bls_g2{};
   auto scalar = scalar_from_big_endian(expanded[0]);
   static_cast<void>(bls_fp_mod(scalar, field[0]));
   scalar = scalar_from_big_endian(expanded[1]);
   static_cast<void>(bls_fp_mod(scalar, field[1]));
   static_cast<void>(bls_g2_map(field, first));
   scalar = scalar_from_big_endian(expanded[2]);
   static_cast<void>(bls_fp_mod(scalar, field[0]));
   scalar = scalar_from_big_endian(expanded[3]);
   static_cast<void>(bls_fp_mod(scalar, field[1]));
   static_cast<void>(bls_g2_map(field, second));
   static_cast<void>(bls_g2_add(first, second, result));
}

} // namespace detail

[[nodiscard]] inline std::string encode_g1_to_bls_public_key(const bls_g1& value) {
   return detail::encode_bls_value(value, detail::bls_public_key_prefix);
}

[[nodiscard]] inline bls_g1 decode_bls_public_key_to_g1(std::string_view value) {
   return detail::decode_bls_value<bls_g1>(value, detail::bls_public_key_prefix);
}

[[nodiscard]] inline std::string encode_g2_to_bls_signature(const bls_g2& value) {
   return detail::encode_bls_value(value, detail::bls_signature_prefix);
}

[[nodiscard]] inline bls_g2 decode_bls_signature_to_g2(std::string_view value) {
   return detail::decode_bls_value<bls_g2>(value, detail::bls_signature_prefix);
}

[[nodiscard]] inline bool bls_pop_verify(const bls_g1& public_key, const bls_g2& proof) {
   auto first = std::array<bls_g1, 2>{};
   auto second = std::array<bls_g2, 2>{};
   std::copy(detail::g1_one_negative.begin(), detail::g1_one_negative.end(), first[0].begin());
   second[0] = proof;
   first[1] = public_key;
   detail::g2_from_message(public_key, detail::bls_pop_ciphersuite, second[1]);
   auto result = bls_gt{};
   static_cast<void>(bls_pairing(first.data(), second.data(), first.size(), result));
   return std::memcmp(result.data(), detail::gt_one.data(), detail::gt_one.size()) == 0;
}

[[nodiscard]] inline bool bls_signature_verify(const bls_g1& public_key, const bls_g2& signature,
                                               std::string_view message) {
   auto first = std::array<bls_g1, 2>{};
   auto second = std::array<bls_g2, 2>{};
   std::copy(detail::g1_one_negative.begin(), detail::g1_one_negative.end(), first[0].begin());
   second[0] = signature;
   first[1] = public_key;
   detail::g2_from_message(std::span<const char>{message.data(), message.size()}, detail::bls_signature_ciphersuite,
                           second[1]);
   auto result = bls_gt{};
   static_cast<void>(bls_pairing(first.data(), second.data(), first.size(), result));
   return std::memcmp(result.data(), detail::gt_one.data(), detail::gt_one.size()) == 0;
}

} // namespace forge::contract
