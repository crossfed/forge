#pragma once

#include <algorithm>

#include <eosio/fixed_bytes.hpp>

import forge.contract.crypto;

namespace eosio {

using forge::contract::ecc_public_key;
using forge::contract::ecc_signature;
using forge::contract::public_key;
using forge::contract::signature;
using forge::contract::webauthn_public_key;
using forge::contract::webauthn_signature;

namespace detail {

template <typename Digest, std::size_t Size> [[nodiscard]] Digest to_digest(const fixed_bytes<Size>& value) {
   static_assert(Digest::byte_size == Size);
   auto result = Digest{};
   const auto bytes = value.extract_as_byte_array();
   std::copy(bytes.begin(), bytes.end(), result.data());
   return result;
}

template <std::size_t Size, typename Digest> [[nodiscard]] fixed_bytes<Size> from_digest(const Digest& value) {
   static_assert(Digest::byte_size == Size);
   return fixed_bytes<Size>{value.extract_as_byte_array()};
}

} // namespace detail

inline void assert_sha256(const char* data, std::uint32_t size, const checksum256& expected) {
   forge::contract::assert_sha256(data, size, detail::to_digest<forge::contract::checksum256>(expected));
}

inline void assert_sha1(const char* data, std::uint32_t size, const checksum160& expected) {
   forge::contract::assert_sha1(data, size, detail::to_digest<forge::contract::checksum160>(expected));
}

inline void assert_sha512(const char* data, std::uint32_t size, const checksum512& expected) {
   forge::contract::assert_sha512(data, size, detail::to_digest<forge::contract::checksum512>(expected));
}

inline void assert_ripemd160(const char* data, std::uint32_t size, const checksum160& expected) {
   forge::contract::assert_ripemd160(data, size, detail::to_digest<forge::contract::checksum160>(expected));
}

[[nodiscard]] inline checksum256 sha256(const char* data, std::uint32_t size) {
   return detail::from_digest<32>(forge::contract::sha256(data, size));
}

[[nodiscard]] inline checksum160 sha1(const char* data, std::uint32_t size) {
   return detail::from_digest<20>(forge::contract::sha1(data, size));
}

[[nodiscard]] inline checksum512 sha512(const char* data, std::uint32_t size) {
   return detail::from_digest<64>(forge::contract::sha512(data, size));
}

[[nodiscard]] inline checksum160 ripemd160(const char* data, std::uint32_t size) {
   return detail::from_digest<20>(forge::contract::ripemd160(data, size));
}

[[nodiscard]] inline public_key recover_key(const checksum256& digest, const signature& value) {
   return forge::contract::recover_key(detail::to_digest<forge::contract::checksum256>(digest), value);
}

inline void assert_recover_key(const checksum256& digest, const signature& value, const public_key& expected) {
   forge::contract::assert_recover_key(detail::to_digest<forge::contract::checksum256>(digest), value, expected);
}

} // namespace eosio
