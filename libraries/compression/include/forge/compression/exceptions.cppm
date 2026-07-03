module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.compression.exceptions;

export import forge.exceptions;

export namespace forge::compression::exceptions {

enum class code : std::uint16_t {
   invalid_input = 1,
   output_limit = 2,
   backend_error = 3,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.compression")

using invalid_input = forge::exceptions::coded_exception<code, code::invalid_input>;
using output_limit = forge::exceptions::coded_exception<code, code::output_limit>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace forge::compression::exceptions
