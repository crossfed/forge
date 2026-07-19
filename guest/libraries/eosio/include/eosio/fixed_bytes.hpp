#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

import forge.chain.protocol.fixed_key;

namespace eosio {

template <std::size_t Size> class fixed_bytes : public forge::chain::protocol::fixed_key<Size> {
 public:
   using base = forge::chain::protocol::fixed_key<Size>;
   using base::base;

   constexpr fixed_bytes() = default;
   constexpr fixed_bytes(const base& value) : base(value) {}
   constexpr fixed_bytes(base&& value) : base(static_cast<base&&>(value)) {}
};

template <typename Stream, std::size_t Size> void raw_pack(Stream& stream, const fixed_bytes<Size>& value) {
   forge::chain::protocol::operator<<(stream, static_cast<const typename fixed_bytes<Size>::base&>(value));
}

template <typename Stream, std::size_t Size> void raw_unpack(Stream& stream, fixed_bytes<Size>& value) {
   forge::chain::protocol::operator>>(stream, static_cast<typename fixed_bytes<Size>::base&>(value));
}

using checksum160 = fixed_bytes<20>;
using checksum256 = fixed_bytes<32>;
using checksum512 = fixed_bytes<64>;

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

} // namespace eosio
