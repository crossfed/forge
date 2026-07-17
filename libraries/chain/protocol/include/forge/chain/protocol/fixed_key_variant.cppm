module;

#include <forge/exceptions/macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

export module forge.chain.protocol.fixed_key:variant;

import :value;
import forge.crypto.hex;
import forge.variant.exceptions;
import forge.variant.value;

export namespace forge {

template <std::size_t Size>
void to_variant(const forge::chain::protocol::fixed_key<Size>& value, forge::variant& output) {
   const auto bytes = value.extract_as_byte_array();
   output = forge::crypto::to_hex(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

template <std::size_t Size>
void from_variant(const forge::variant& input, forge::chain::protocol::fixed_key<Size>& output) {
   const auto& text = input.get_string();
   if (text.size() != Size * 2U) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "chain fixed key has invalid hex length");
   }
   auto bytes = std::array<std::uint8_t, Size>{};
   try {
      if (forge::crypto::from_hex(text, bytes.data(), bytes.size()) != bytes.size()) {
         FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "chain fixed key has invalid hex");
      }
   } catch (const forge::crypto::hex::exceptions::invalid_character&) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "chain fixed key has invalid hex");
   }
   output = forge::chain::protocol::fixed_key<Size>{bytes};
}

} // namespace forge
