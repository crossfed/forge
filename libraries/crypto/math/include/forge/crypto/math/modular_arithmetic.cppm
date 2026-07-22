module;
#include <forge/exceptions/macros.hpp>
#include <cstdint>

export module forge.crypto.math.modular_arithmetic;

export import forge.exceptions;
import forge.crypto.core.types;

export namespace forge::crypto::math::modular_arithmetic::exceptions {

enum class code : std::uint16_t {
   invalid_modulus = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.math.modular_arithmetic")

using invalid_modulus = forge::exceptions::coded_exception<code, code::invalid_modulus>;

} // namespace forge::crypto::math::modular_arithmetic::exceptions

export namespace forge::crypto::math {

core::bytes modexp(const core::bytes& _base, const core::bytes& _exponent, const core::bytes& _modulus);
} // namespace forge::crypto
