module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.chain.transaction.exceptions;

export import forge.exceptions;

export namespace forge::chain::transaction::exceptions {

enum class code : std::uint16_t {
   invalid_context = 1,
   invalid_options = 2,
   missing_action = 3,
   signer_mismatch = 4,
   duplicate_signature = 5,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.chain.transaction")

using invalid_context = forge::exceptions::coded_exception<code, code::invalid_context>;
using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using missing_action = forge::exceptions::coded_exception<code, code::missing_action>;
using signer_mismatch = forge::exceptions::coded_exception<code, code::signer_mismatch>;
using duplicate_signature = forge::exceptions::coded_exception<code, code::duplicate_signature>;

} // namespace forge::chain::transaction::exceptions
