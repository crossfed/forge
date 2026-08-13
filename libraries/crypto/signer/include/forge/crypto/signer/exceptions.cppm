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
   invalid_config = 5,
   invalid_source = 6,
   source_too_large = 7,
   insecure_permissions = 8,
   invalid_key = 9,
   io_error = 10,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.signer")

using unavailable = forge::exceptions::coded_exception<code, code::unavailable>;
using unknown_key = forge::exceptions::coded_exception<code, code::unknown_key>;
using invalid_signature = forge::exceptions::coded_exception<code, code::invalid_signature>;
using forbidden = forge::exceptions::coded_exception<code, code::forbidden>;
using invalid_config = forge::exceptions::coded_exception<code, code::invalid_config>;
using invalid_source = forge::exceptions::coded_exception<code, code::invalid_source>;
using source_too_large = forge::exceptions::coded_exception<code, code::source_too_large>;
using insecure_permissions = forge::exceptions::coded_exception<code, code::insecure_permissions>;
using invalid_key = forge::exceptions::coded_exception<code, code::invalid_key>;
using io_error = forge::exceptions::coded_exception<code, code::io_error>;

} // namespace forge::crypto::signer::exceptions
