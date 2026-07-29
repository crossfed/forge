module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.contract.testing.exceptions;

export import forge.exceptions;

export namespace forge::contract::testing::exceptions {

enum class code : std::uint16_t {
   assertion_failure = 1,
   database_error = 2,
   invalid_iterator = 3,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.contract.testing")

using assertion_failure = forge::exceptions::coded_exception<code, code::assertion_failure>;
using database_error = forge::exceptions::coded_exception<code, code::database_error>;
using invalid_iterator = forge::exceptions::coded_exception<code, code::invalid_iterator>;

} // namespace forge::contract::testing::exceptions
