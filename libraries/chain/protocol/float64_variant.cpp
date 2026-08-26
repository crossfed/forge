module;

#include <forge/exceptions/macros.hpp>

#include <bit>
#include <cstdint>
#include <stdexcept>
#include <typeinfo>

module forge.chain.protocol.float64;

import :value;
import forge.variant.exceptions;
import forge.variant.value;

namespace forge {

void to_variant(const forge::chain::protocol::float64& value, forge::variant& output) {
   output = std::bit_cast<double>(value.bits);
}

void from_variant(const forge::variant& input, forge::chain::protocol::float64& output) {
   try {
      output.bits = std::bit_cast<std::uint64_t>(input.as_double());
   } catch (const std::bad_cast&) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "float64 variant must contain a numeric value");
   } catch (const std::invalid_argument&) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "float64 variant has invalid numeric data");
   } catch (const std::out_of_range&) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "float64 variant numeric data is out of range");
   }
}

} // namespace forge
