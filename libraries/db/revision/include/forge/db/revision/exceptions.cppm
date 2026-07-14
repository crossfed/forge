module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.db.revision.exceptions;

export import forge.exceptions;

export namespace forge::db::revision::exceptions {

enum class code : std::uint16_t {
   invalid_store = 1,
   unsupported_operation = 2,
   corrupt_state = 3,
   stale_head = 4,
   revision_overflow = 5,
   prune_limit_too_small = 6,
   invalid_prune = 7,
   transaction_closed = 8,
   revision_pruned = 9,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.db.revision")

using invalid_store = forge::exceptions::coded_exception<code, code::invalid_store>;
using unsupported_operation = forge::exceptions::coded_exception<code, code::unsupported_operation>;
using corrupt_state = forge::exceptions::coded_exception<code, code::corrupt_state>;
using stale_head = forge::exceptions::coded_exception<code, code::stale_head>;
using revision_overflow = forge::exceptions::coded_exception<code, code::revision_overflow>;
using prune_limit_too_small = forge::exceptions::coded_exception<code, code::prune_limit_too_small>;
using invalid_prune = forge::exceptions::coded_exception<code, code::invalid_prune>;
using transaction_closed = forge::exceptions::coded_exception<code, code::transaction_closed>;
using revision_pruned = forge::exceptions::coded_exception<code, code::revision_pruned>;

} // namespace forge::db::revision::exceptions
