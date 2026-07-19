module;

#include <forge/contract/intrinsics.h>

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

export module forge.contract.crypto;

export import forge.contract.fixed_bytes;

import forge.crypto.asymmetric;
import forge.contract.datastream;
import forge.contract.intrinsics;

export namespace forge::contract {

using ecc_public_key = forge::crypto::asymmetric::ecc_public_key;
using ecc_signature = forge::crypto::asymmetric::ecc_signature;
using webauthn_public_key = forge::crypto::asymmetric::webauthn_public_key;
using webauthn_signature = forge::crypto::asymmetric::webauthn_signature;
using public_key = forge::crypto::asymmetric::public_key;
using signature = forge::crypto::asymmetric::signature;

inline void assert_sha256(const char* data, std::uint32_t size, const checksum256& expected) {
   ::assert_sha256(data, size, reinterpret_cast<const capi_checksum256*>(expected.data()));
}

inline void assert_sha1(const char* data, std::uint32_t size, const checksum160& expected) {
   ::assert_sha1(data, size, reinterpret_cast<const capi_checksum160*>(expected.data()));
}

inline void assert_sha512(const char* data, std::uint32_t size, const checksum512& expected) {
   ::assert_sha512(data, size, reinterpret_cast<const capi_checksum512*>(expected.data()));
}

inline void assert_ripemd160(const char* data, std::uint32_t size, const checksum160& expected) {
   ::assert_ripemd160(data, size, reinterpret_cast<const capi_checksum160*>(expected.data()));
}

[[nodiscard]] inline checksum256 sha256(const char* data, std::uint32_t size) {
   auto result = checksum256{};
   ::sha256(data, size, reinterpret_cast<capi_checksum256*>(result.data()));
   return result;
}

[[nodiscard]] inline checksum160 sha1(const char* data, std::uint32_t size) {
   auto result = checksum160{};
   ::sha1(data, size, reinterpret_cast<capi_checksum160*>(result.data()));
   return result;
}

[[nodiscard]] inline checksum512 sha512(const char* data, std::uint32_t size) {
   auto result = checksum512{};
   ::sha512(data, size, reinterpret_cast<capi_checksum512*>(result.data()));
   return result;
}

[[nodiscard]] inline checksum160 ripemd160(const char* data, std::uint32_t size) {
   auto result = checksum160{};
   ::ripemd160(data, size, reinterpret_cast<capi_checksum160*>(result.data()));
   return result;
}

[[nodiscard]] inline public_key recover_key(const checksum256& digest, const signature& value) {
   const auto packed = ::forge::raw::pack(value);
   const auto required = ::recover_key(
       reinterpret_cast<const capi_checksum256*>(digest.data()), reinterpret_cast<const char*>(packed.data()),
       packed.size(), nullptr, 0U);
   check(required > 0, "recover_key failed");
   auto bytes = std::vector<std::uint8_t>(static_cast<std::size_t>(required));
   const auto written = ::recover_key(
       reinterpret_cast<const capi_checksum256*>(digest.data()), reinterpret_cast<const char*>(packed.data()),
       packed.size(), reinterpret_cast<char*>(bytes.data()), bytes.size());
   check(written == required, "recover_key returned inconsistent public key size");
   return ::forge::raw::unpack_exact<public_key>(bytes);
}

inline void assert_recover_key(const checksum256& digest, const signature& value, const public_key& expected) {
   const auto packed_signature = ::forge::raw::pack(value);
   const auto packed_key = ::forge::raw::pack(expected);
   ::assert_recover_key(reinterpret_cast<const capi_checksum256*>(digest.data()),
                        reinterpret_cast<const char*>(packed_signature.data()), packed_signature.size(),
                        reinterpret_cast<const char*>(packed_key.data()), packed_key.size());
}

} // namespace forge::contract
