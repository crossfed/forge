#pragma once

#include <eosio/dispatcher.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

import forge.raw.codec;

namespace eosio {

using ::forge::raw::pack_size;

template <typename T> [[nodiscard]] std::vector<char> pack(const T& value) {
   const auto bytes = ::forge::raw::pack(value);
   return {reinterpret_cast<const char*>(bytes.data()), reinterpret_cast<const char*>(bytes.data() + bytes.size())};
}

template <typename T> [[nodiscard]] T unpack(const char* data, std::size_t size) {
   return ::forge::raw::unpack_exact<T>(
       std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(data), size});
}

template <typename T> [[nodiscard]] T unpack(const std::vector<char>& data) {
   return unpack<T>(data.data(), data.size());
}

template <typename T> void unpack(T& value, const char* data, std::size_t size) {
   value = unpack<T>(data, size);
}

} // namespace eosio

#define FORGE_EOSIO_DETAIL_PACK_MEMBER(type, member) ::forge::raw::pack(stream, value.member);
#define FORGE_EOSIO_DETAIL_UNPACK_MEMBER(type, member) ::forge::raw::unpack(stream, value.member);

#define EOSLIB_SERIALIZE(type, members)                                                                                \
   template <typename Stream> friend void raw_pack(Stream& stream, const type& value) {                                \
      FORGE_EOSIO_DETAIL_FOREACH_SEQ(FORGE_EOSIO_DETAIL_PACK_MEMBER, type, members)                                    \
   }                                                                                                                   \
   template <typename Stream> friend void raw_unpack(Stream& stream, type& value) {                                    \
      FORGE_EOSIO_DETAIL_FOREACH_SEQ(FORGE_EOSIO_DETAIL_UNPACK_MEMBER, type, members)                                  \
   }

#define EOSLIB_SERIALIZE_DERIVED(type, base, members)                                                                  \
   template <typename Stream> friend void raw_pack(Stream& stream, const type& value) {                                \
      ::forge::raw::pack(stream, static_cast<const base&>(value));                                                     \
      FORGE_EOSIO_DETAIL_FOREACH_SEQ(FORGE_EOSIO_DETAIL_PACK_MEMBER, type, members)                                    \
   }                                                                                                                   \
   template <typename Stream> friend void raw_unpack(Stream& stream, type& value) {                                    \
      ::forge::raw::unpack(stream, static_cast<base&>(value));                                                         \
      FORGE_EOSIO_DETAIL_FOREACH_SEQ(FORGE_EOSIO_DETAIL_UNPACK_MEMBER, type, members)                                  \
   }
