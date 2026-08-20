module;

#include <utility>

export module forge.auth.pairing.pairing;

export import forge.auth.pairing.exceptions;
export import forge.auth.pairing.types;

export namespace forge::auth::pairing {

[[nodiscard]] scope_set canonicalize_scopes(scope_set scopes);

[[nodiscard]] bootstrap_issuance begin_bootstrap(bootstrap_options options);

[[nodiscard]] pending_request consume_bootstrap(bootstrap_record& bootstrap,
                                                const forge::crypto::core::secret_string& token,
                                                pairing_request request, consume_options options);

[[nodiscard]] pending_request supersede_pending(pending_request& pending, pairing_request replacement, time_point now);

[[nodiscard]] credential approve_pending(pending_request& pending, approval_options options);
void reject_pending(pending_request& pending, time_point now);

void rotate_credential_downscope(credential& value, scope_set scopes, time_point now);
void revoke_credential(credential& value, time_point now);

} // namespace forge::auth::pairing
