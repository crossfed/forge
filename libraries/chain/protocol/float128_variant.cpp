module;

#include <forge/exceptions/macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

module forge.chain.protocol.float128;

import :value;
import forge.codec.hex;
import forge.variant.exceptions;
import forge.variant.value;

namespace {

[[nodiscard]] std::array<std::uint8_t, 16>
float128_little_endian_bytes(forge::chain::protocol::uint128_t bits) noexcept {
   auto bytes = std::array<std::uint8_t, 16>{};
   for (auto index = std::size_t{}; index < bytes.size(); ++index) {
      bytes[index] = static_cast<std::uint8_t>((bits >> static_cast<unsigned>(index * 8U)) & 0xffU);
   }
   return bytes;
}

} // namespace

namespace forge {

void to_variant(const forge::chain::protocol::float128& value, forge::variant& output) {
   const auto bytes = float128_little_endian_bytes(value.bits);
   output = std::string{"0x"} + forge::codec::hex::encode(std::span<const std::uint8_t>{bytes});
}

void from_variant(const forge::variant& input, forge::chain::protocol::float128& output) {
   if (!input.is_string()) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "float128 variant must contain a string");
   }

   const auto& text = input.get_string();
   if (text.size() != 34U || !text.starts_with("0x")) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error,
                            "float128 variant must use 0x followed by 32 hexadecimal digits");
   }

   auto bytes = std::array<std::uint8_t, 16>{};
   try {
      if (forge::codec::hex::decode(text.substr(2U), std::span<std::uint8_t>{bytes}) != bytes.size()) {
         FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "float128 variant has invalid hexadecimal data");
      }
   } catch (const forge::codec::hex::exceptions::invalid_input&) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "float128 variant has invalid hexadecimal data");
   } catch (const forge::codec::hex::exceptions::insufficient_output&) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "float128 variant has invalid hexadecimal data");
   } catch (const std::invalid_argument&) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "float128 variant has invalid hexadecimal data");
   } catch (const std::out_of_range&) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "float128 variant has invalid hexadecimal data");
   }

   auto bits = forge::chain::protocol::uint128_t{};
   for (auto index = std::size_t{}; index < bytes.size(); ++index) {
      bits |= static_cast<forge::chain::protocol::uint128_t>(bytes[index]) << static_cast<unsigned>(index * 8U);
   }
   output.bits = bits;
}

} // namespace forge
