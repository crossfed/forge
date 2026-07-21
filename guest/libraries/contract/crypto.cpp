module;

#include <forge/contract/internal/intrinsics.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

module forge.contract.crypto;

import forge.contract.intrinsics;
import forge.raw.codec;

namespace forge::contract {

void assert_sha256(const char* data, std::uint32_t size, const checksum256& expected) {
   internal::assert_sha256(data, size, reinterpret_cast<const capi_checksum256*>(expected.data()));
}

void assert_sha1(const char* data, std::uint32_t size, const checksum160& expected) {
   internal::assert_sha1(data, size, reinterpret_cast<const capi_checksum160*>(expected.data()));
}

void assert_sha512(const char* data, std::uint32_t size, const checksum512& expected) {
   internal::assert_sha512(data, size, reinterpret_cast<const capi_checksum512*>(expected.data()));
}

void assert_ripemd160(const char* data, std::uint32_t size, const checksum160& expected) {
   internal::assert_ripemd160(data, size, reinterpret_cast<const capi_checksum160*>(expected.data()));
}

checksum256 sha256(const char* data, std::uint32_t size) {
   auto result = checksum256{};
   internal::sha256(data, size, reinterpret_cast<capi_checksum256*>(result.data()));
   return result;
}

checksum160 sha1(const char* data, std::uint32_t size) {
   auto result = checksum160{};
   internal::sha1(data, size, reinterpret_cast<capi_checksum160*>(result.data()));
   return result;
}

checksum512 sha512(const char* data, std::uint32_t size) {
   auto result = checksum512{};
   internal::sha512(data, size, reinterpret_cast<capi_checksum512*>(result.data()));
   return result;
}

checksum160 ripemd160(const char* data, std::uint32_t size) {
   auto result = checksum160{};
   internal::ripemd160(data, size, reinterpret_cast<capi_checksum160*>(result.data()));
   return result;
}

public_key recover_key(const checksum256& digest, const signature& value) {
   const auto packed = forge::raw::pack(value);
   auto optimistic = std::array<std::uint8_t, 256>{};
   const auto required = internal::recover_key(reinterpret_cast<const capi_checksum256*>(digest.data()),
                                               reinterpret_cast<const char*>(packed.data()), packed.size(),
                                               reinterpret_cast<char*>(optimistic.data()), optimistic.size());
   check(required > 0, "recover_key failed");
   if (static_cast<std::size_t>(required) <= optimistic.size()) {
      return forge::raw::unpack_exact<public_key>(
          std::span<const std::uint8_t>{optimistic.data(), static_cast<std::size_t>(required)});
   }

   auto bytes = std::vector<std::uint8_t>(static_cast<std::size_t>(required));
   const auto written = internal::recover_key(reinterpret_cast<const capi_checksum256*>(digest.data()),
                                              reinterpret_cast<const char*>(packed.data()), packed.size(),
                                              reinterpret_cast<char*>(bytes.data()), bytes.size());
   check(written == required, "recover_key returned inconsistent public key size");
   return forge::raw::unpack_exact<public_key>(bytes);
}

void assert_recover_key(const checksum256& digest, const signature& value, const public_key& expected) {
   const auto packed_signature = forge::raw::pack(value);
   const auto packed_key = forge::raw::pack(expected);
   internal::assert_recover_key(reinterpret_cast<const capi_checksum256*>(digest.data()),
                                reinterpret_cast<const char*>(packed_signature.data()), packed_signature.size(),
                                reinterpret_cast<const char*>(packed_key.data()), packed_key.size());
}

} // namespace forge::contract
