module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.chain.savanna.exceptions;

export import forge.exceptions;

export namespace forge::chain::savanna::exceptions {

enum class code : std::uint16_t {
   invalid_policy = 1,
   policy_weight_overflow = 2,
   duplicate_finalizer = 3,
   invalid_finality_state = 4,
   invalid_qc = 5,
   invalid_qc_signature = 6,
   invalid_validation_state = 7,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.chain.savanna")

using invalid_policy = forge::exceptions::coded_exception<code, code::invalid_policy>;
using policy_weight_overflow = forge::exceptions::coded_exception<code, code::policy_weight_overflow>;
using duplicate_finalizer = forge::exceptions::coded_exception<code, code::duplicate_finalizer>;
using invalid_finality_state = forge::exceptions::coded_exception<code, code::invalid_finality_state>;
using invalid_qc = forge::exceptions::coded_exception<code, code::invalid_qc>;
using invalid_qc_signature = forge::exceptions::coded_exception<code, code::invalid_qc_signature>;
using invalid_validation_state = forge::exceptions::coded_exception<code, code::invalid_validation_state>;

} // namespace forge::chain::savanna::exceptions
