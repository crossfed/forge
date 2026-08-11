module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.crypto.bls.exceptions;

export import forge.exceptions;

export namespace forge::crypto::bls::exceptions {

enum class code : std::uint16_t {
   parse_error = 1,
   invalid_private_key = 2,
   invalid_signature = 3,
   invalid_accumulator = 4,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.bls")

using parse_error = forge::exceptions::coded_exception<code, code::parse_error>;
using invalid_private_key = forge::exceptions::coded_exception<code, code::invalid_private_key>;
using invalid_signature = forge::exceptions::coded_exception<code, code::invalid_signature>;
using invalid_accumulator = forge::exceptions::coded_exception<code, code::invalid_accumulator>;

} // namespace forge::crypto::bls::exceptions
