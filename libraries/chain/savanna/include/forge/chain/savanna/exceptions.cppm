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
   invalid_proof_of_possession = 8,
   validation_root_unavailable = 9,
   invalid_finalizer_safety_state = 10,
   invalid_genesis = 11,
   invalid_extension = 12,
   invalid_header = 13,
   invalid_merkle = 14,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.chain.savanna")

using invalid_policy = forge::exceptions::coded_exception<code, code::invalid_policy>;
using policy_weight_overflow = forge::exceptions::coded_exception<code, code::policy_weight_overflow>;
using duplicate_finalizer = forge::exceptions::coded_exception<code, code::duplicate_finalizer>;
using invalid_finality_state = forge::exceptions::coded_exception<code, code::invalid_finality_state>;
using invalid_qc = forge::exceptions::coded_exception<code, code::invalid_qc>;
using invalid_qc_signature = forge::exceptions::coded_exception<code, code::invalid_qc_signature>;
using invalid_validation_state = forge::exceptions::coded_exception<code, code::invalid_validation_state>;
using invalid_proof_of_possession = forge::exceptions::coded_exception<code, code::invalid_proof_of_possession>;
using validation_root_unavailable = forge::exceptions::coded_exception<code, code::validation_root_unavailable>;
using invalid_finalizer_safety_state = forge::exceptions::coded_exception<code, code::invalid_finalizer_safety_state>;
using invalid_genesis = forge::exceptions::coded_exception<code, code::invalid_genesis>;
using invalid_extension = forge::exceptions::coded_exception<code, code::invalid_extension>;
using invalid_header = forge::exceptions::coded_exception<code, code::invalid_header>;
using invalid_merkle = forge::exceptions::coded_exception<code, code::invalid_merkle>;

} // namespace forge::chain::savanna::exceptions
