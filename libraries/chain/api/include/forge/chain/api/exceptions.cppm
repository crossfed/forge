module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.chain.api.exceptions;

export import forge.exceptions;

export namespace forge::chain::api::exceptions {

enum class code : std::uint16_t {
   invalid_request = 1,
   audit_not_supported = 2,
   anchor_unavailable = 3,
   wrong_chain = 4,
   invalid_finality = 5,
   invalid_state_proof = 6,
   invalid_transaction_proof = 7,
   trust_required = 8,
   history_lost = 9,
   deadline_exceeded = 10,
   unavailable = 11,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.chain.api")

using invalid_request =
   forge::exceptions::coded_exception<code, code::invalid_request>;
using audit_not_supported =
   forge::exceptions::coded_exception<code, code::audit_not_supported>;
using anchor_unavailable =
   forge::exceptions::coded_exception<code, code::anchor_unavailable>;
using wrong_chain =
   forge::exceptions::coded_exception<code, code::wrong_chain>;
using invalid_finality =
   forge::exceptions::coded_exception<code, code::invalid_finality>;
using invalid_state_proof =
   forge::exceptions::coded_exception<code, code::invalid_state_proof>;
using invalid_transaction_proof =
   forge::exceptions::coded_exception<code, code::invalid_transaction_proof>;
using trust_required =
   forge::exceptions::coded_exception<code, code::trust_required>;
using history_lost =
   forge::exceptions::coded_exception<code, code::history_lost>;
using deadline_exceeded =
   forge::exceptions::coded_exception<code, code::deadline_exceeded>;
using unavailable =
   forge::exceptions::coded_exception<code, code::unavailable>;

} // namespace forge::chain::api::exceptions
