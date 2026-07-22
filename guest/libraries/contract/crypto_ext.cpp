module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstdint>
#include <vector>

module forge.contract.crypto_ext;

import forge.contract.intrinsics;

namespace forge::contract {

std::int32_t alt_bn128_add(const char* first, std::uint32_t first_size, const char* second, std::uint32_t second_size,
                           char* result, std::uint32_t result_size) {
   return internal::alt_bn128_add(first, first_size, second, second_size, result, result_size);
}

std::int32_t alt_bn128_mul(const char* point, std::uint32_t point_size, const char* scalar, std::uint32_t scalar_size,
                           char* result, std::uint32_t result_size) {
   return internal::alt_bn128_mul(point, point_size, scalar, scalar_size, result, result_size);
}

std::int32_t alt_bn128_pair(const char* pairs, std::uint32_t size) {
   return internal::alt_bn128_pair(pairs, size);
}

std::int32_t mod_exp(const char* base, std::uint32_t base_size, const char* exponent, std::uint32_t exponent_size,
                     const char* modulus, std::uint32_t modulus_size, char* result, std::uint32_t result_size) {
   return internal::mod_exp(base, base_size, exponent, exponent_size, modulus, modulus_size, result, result_size);
}

std::int32_t mod_exp(const bigint& base, const bigint& exponent, const bigint& modulus, bigint& result) {
   check(result.size() >= modulus.size(), "mod_exp result parameter's size must be >= mod's size");
   return internal::mod_exp(base.data(), base.size(), exponent.data(), exponent.size(), modulus.data(), modulus.size(),
                            result.data(), result.size());
}

std::int32_t blake2_f(std::uint32_t rounds, const char* state, std::uint32_t state_size, const char* message,
                      std::uint32_t message_size, const char* offset0, std::uint32_t offset0_size, const char* offset1,
                      std::uint32_t offset1_size, std::int32_t final, char* result, std::uint32_t result_size) {
   return internal::blake2_f(rounds, state, state_size, message, message_size, offset0, offset0_size, offset1,
                             offset1_size, final, result, result_size);
}

std::int32_t blake2_f(std::uint32_t rounds, const std::vector<char>& state, const std::vector<char>& message,
                      const std::vector<char>& offset0, const std::vector<char>& offset1, bool final,
                      std::vector<char>& result) {
   check(result.size() >= blake2f_result_size, "blake2_f result parameter's size must be >= 64");
   return internal::blake2_f(rounds, state.data(), state.size(), message.data(), message.size(), offset0.data(),
                             offset0.size(), offset1.data(), offset1.size(), final, result.data(), result.size());
}

checksum256 sha3(const char* data, std::uint32_t size) {
   auto result = checksum256{};
   internal::sha3(data, size, result.data(), result.data_size(), 0);
   return result;
}

void assert_sha3(const char* data, std::uint32_t size, const checksum256& expected) {
   check(sha3(data, size) == expected, "SHA3 hash of `data` does not match given `hash`");
}

checksum256 keccak(const char* data, std::uint32_t size) {
   auto result = checksum256{};
   internal::sha3(data, size, result.data(), result.data_size(), 1);
   return result;
}

void assert_keccak(const char* data, std::uint32_t size, const checksum256& expected) {
   check(keccak(data, size) == expected, "Keccak hash of `data` does not match given `hash`");
}

std::int32_t k1_recover(const char* signature, std::uint32_t signature_size, const char* digest,
                        std::uint32_t digest_size, char* public_key, std::uint32_t public_key_size) {
   return internal::k1_recover(signature, signature_size, digest, digest_size, public_key, public_key_size);
}

} // namespace forge::contract
