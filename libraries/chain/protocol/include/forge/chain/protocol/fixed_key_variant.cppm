module;

#include <forge/exceptions/macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

export module forge.chain.protocol.fixed_key:variant;

import :value;
import forge.codec.hex;
import forge.variant.exceptions;
import forge.variant.value;

export namespace forge {

template <std::size_t Size>
void to_variant(const forge::chain::protocol::fixed_key<Size>& value, forge::variant& output) {
   const auto bytes = value.extract_as_byte_array();
   output = forge::codec::hex::encode(bytes);
}

template <std::size_t Size>
void from_variant(const forge::variant& input, forge::chain::protocol::fixed_key<Size>& output) {
   const auto& text = input.get_string();
   if (text.size() != Size * 2U) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "chain fixed key has invalid hex length");
   }
   auto bytes = std::array<std::uint8_t, Size>{};
   try {
      if (forge::codec::hex::decode(text, bytes) != bytes.size()) {
         FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "chain fixed key has invalid hex");
      }
   } catch (const forge::codec::hex::exceptions::invalid_input&) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "chain fixed key has invalid hex");
   }
   output = forge::chain::protocol::fixed_key<Size>{bytes};
}

} // namespace forge
