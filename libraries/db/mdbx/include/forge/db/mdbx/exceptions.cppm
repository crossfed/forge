module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.db.mdbx.exceptions;

import forge.exceptions;

export namespace forge::db::mdbx::exceptions {

enum class code : std::uint16_t {
   invalid_config = 1,
   open_failed = 2,
   environment_busy = 3,
   incompatible_environment = 4,
   invalid_family = 5,
   map_full = 6,
   readers_full = 7,
   transaction_full = 8,
   key_too_large = 9,
   value_too_large = 10,
   corruption = 11,
   io_error = 12,
   native_error = 13,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.db.mdbx")

using invalid_config = forge::exceptions::coded_exception<code, code::invalid_config>;
using open_failed = forge::exceptions::coded_exception<code, code::open_failed>;
using environment_busy = forge::exceptions::coded_exception<code, code::environment_busy>;
using incompatible_environment = forge::exceptions::coded_exception<code, code::incompatible_environment>;
using invalid_family = forge::exceptions::coded_exception<code, code::invalid_family>;
using map_full = forge::exceptions::coded_exception<code, code::map_full>;
using readers_full = forge::exceptions::coded_exception<code, code::readers_full>;
using transaction_full = forge::exceptions::coded_exception<code, code::transaction_full>;
using key_too_large = forge::exceptions::coded_exception<code, code::key_too_large>;
using value_too_large = forge::exceptions::coded_exception<code, code::value_too_large>;
using corruption = forge::exceptions::coded_exception<code, code::corruption>;
using io_error = forge::exceptions::coded_exception<code, code::io_error>;
using native_error = forge::exceptions::coded_exception<code, code::native_error>;

} // namespace forge::db::mdbx::exceptions
