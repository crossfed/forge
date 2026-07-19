#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

import forge.chain.protocol.values;

namespace eosio {

struct name : forge::chain::protocol::name {
   using base = forge::chain::protocol::name;
   using raw = base::raw;

   constexpr name() = default;
   constexpr explicit name(std::uint64_t value) : base(value) {}
   constexpr explicit name(raw value) : base(value) {}
   constexpr explicit name(std::string_view value) : base(value) {}
   constexpr name(base value) : base(value) {}
};

template <typename Stream> void raw_pack(Stream& stream, const name& value) {
   forge::chain::protocol::raw_pack(stream, static_cast<const name::base&>(value));
}

template <typename Stream> void raw_unpack(Stream& stream, name& value) {
   forge::chain::protocol::raw_unpack(stream, static_cast<name::base&>(value));
}

namespace literals {

consteval name operator""_n(const char* value, std::size_t size) {
   return name{std::string_view{value, size}};
}

} // namespace literals

} // namespace eosio

using eosio::literals::operator""_n;
