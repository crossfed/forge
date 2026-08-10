module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.cli.exceptions;

export import forge.exceptions;

export namespace forge::cli::exceptions {

enum class code : std::uint16_t {
   invalid_descriptor = 1,
   parse_failed = 2,
   validation_failed = 3,
   dispatch_failed = 4,
   canceled = 5,
   terminal_failed = 6,
   invalid_arguments = 7,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.cli")

using invalid_descriptor = forge::exceptions::coded_exception<code, code::invalid_descriptor>;
using parse_failed = forge::exceptions::coded_exception<code, code::parse_failed>;
using validation_failed = forge::exceptions::coded_exception<code, code::validation_failed>;
using dispatch_failed = forge::exceptions::coded_exception<code, code::dispatch_failed>;
using canceled = forge::exceptions::coded_exception<code, code::canceled>;
using terminal_failed = forge::exceptions::coded_exception<code, code::terminal_failed>;
using invalid_arguments = forge::exceptions::coded_exception<code, code::invalid_arguments>;

} // namespace forge::cli::exceptions
