module;

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

export module forge.contract.crypto;

export import forge.contract.fixed_bytes;

import forge.crypto.asymmetric.values;
import forge.contract.datastream;
import forge.contract.intrinsics;

export namespace forge::contract {

using ecc_public_key = forge::crypto::asymmetric::ecc_public_key;
using ecc_signature = forge::crypto::asymmetric::ecc_signature;
using webauthn_public_key = forge::crypto::asymmetric::webauthn_public_key;
using webauthn_signature = forge::crypto::asymmetric::webauthn_signature;
using public_key = forge::crypto::asymmetric::public_key;
using signature = forge::crypto::asymmetric::signature;

void assert_sha256(const char* data, std::uint32_t size, const checksum256& expected);
void assert_sha1(const char* data, std::uint32_t size, const checksum160& expected);
void assert_sha512(const char* data, std::uint32_t size, const checksum512& expected);
void assert_ripemd160(const char* data, std::uint32_t size, const checksum160& expected);
[[nodiscard]] checksum256 sha256(const char* data, std::uint32_t size);
[[nodiscard]] checksum160 sha1(const char* data, std::uint32_t size);
[[nodiscard]] checksum512 sha512(const char* data, std::uint32_t size);
[[nodiscard]] checksum160 ripemd160(const char* data, std::uint32_t size);
[[nodiscard]] public_key recover_key(const checksum256& digest, const signature& value);
void assert_recover_key(const checksum256& digest, const signature& value, const public_key& expected);

} // namespace forge::contract
