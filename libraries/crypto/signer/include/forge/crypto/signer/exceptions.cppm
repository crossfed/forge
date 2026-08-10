module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.crypto.signer.exceptions;

export import forge.exceptions;

export namespace forge::crypto::signer::exceptions {

enum class code : std::uint16_t {
   unavailable = 1,
   unknown_key = 2,
   invalid_signature = 3,
   forbidden = 4,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.signer")

using unavailable = forge::exceptions::coded_exception<code, code::unavailable>;
using unknown_key = forge::exceptions::coded_exception<code, code::unknown_key>;
using invalid_signature = forge::exceptions::coded_exception<code, code::invalid_signature>;
using forbidden = forge::exceptions::coded_exception<code, code::forbidden>;

} // namespace forge::crypto::signer::exceptions
