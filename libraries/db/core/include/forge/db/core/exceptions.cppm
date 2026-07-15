module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.db.core.exceptions;

export import forge.exceptions;

export namespace forge::db::core::exceptions {

enum class code : std::uint16_t {
   invalid_descriptor = 1,
   invalid_cursor = 2,
   not_found = 3,
   transaction_closed = 4,
   unsupported_operation = 5,
   invalid_savepoint = 6,
   savepoint_overflow = 7,
   transaction_rollback_only = 8,
   participant_conflict = 9,
   mutation_forbidden = 10,
   driver_busy = 11,
   driver_closed = 12,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.db.core")

using invalid_descriptor = forge::exceptions::coded_exception<code, code::invalid_descriptor>;
using invalid_cursor = forge::exceptions::coded_exception<code, code::invalid_cursor>;
using not_found = forge::exceptions::coded_exception<code, code::not_found>;
using transaction_closed = forge::exceptions::coded_exception<code, code::transaction_closed>;
using unsupported_operation = forge::exceptions::coded_exception<code, code::unsupported_operation>;
using invalid_savepoint = forge::exceptions::coded_exception<code, code::invalid_savepoint>;
using savepoint_overflow = forge::exceptions::coded_exception<code, code::savepoint_overflow>;
using transaction_rollback_only = forge::exceptions::coded_exception<code, code::transaction_rollback_only>;
using participant_conflict = forge::exceptions::coded_exception<code, code::participant_conflict>;
using mutation_forbidden = forge::exceptions::coded_exception<code, code::mutation_forbidden>;
using driver_busy = forge::exceptions::coded_exception<code, code::driver_busy>;
using driver_closed = forge::exceptions::coded_exception<code, code::driver_closed>;

} // namespace forge::db::core::exceptions
