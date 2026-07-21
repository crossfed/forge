module;

#include <cstddef>
#include <cstdint>
#include <string_view>

export module forge.contract.compatibility_name;

import forge.chain.protocol.values;
import forge.raw.codec;

export namespace forge::contract::compatibility {

struct name : chain::protocol::name {
   using base = chain::protocol::name;
   using raw = base::raw;

   constexpr name() = default;
   constexpr explicit name(std::uint64_t value) : base(value) {}
   constexpr explicit name(raw value) : base(value) {}
   constexpr explicit name(std::string_view value) : base(value) {}
   constexpr name(base value) : base(value) {}
};

template <typename Stream> void raw_pack(Stream& stream, const name& value) {
   chain::protocol::raw_pack(stream, static_cast<const name::base&>(value));
}

template <typename Stream> void raw_unpack(Stream& stream, name& value) {
   chain::protocol::raw_unpack(stream, static_cast<name::base&>(value));
}

namespace literals {

consteval name operator""_n(const char* value, std::size_t size) {
   return name{std::string_view{value, size}};
}

} // namespace literals

} // namespace forge::contract::compatibility
