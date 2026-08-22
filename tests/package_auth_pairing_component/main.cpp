#include <chrono>
#include <utility>

import forge.auth.pairing.exceptions;
import forge.auth.pairing.pairing;

int main() {
   namespace pairing = forge::auth::pairing;

   const auto now = pairing::time_point{std::chrono::seconds{1}};
   auto issuance = pairing::begin_bootstrap({
       .now = now,
       .expires_at = now + std::chrono::minutes{1},
       .scope_baseline = {"pair.read"},
   });
   auto pending_issuance = pairing::consume_bootstrap(issuance.record, issuance.token,
                                                      {.identity = "consumer", .requested_scopes = {"pair.read"}},
                                                      {.now = now + std::chrono::seconds{1},
                                                       .request_expires_at = now + std::chrono::seconds{30},
                                                       .pending_count = 0,
                                                       .max_pending_requests = 1});
   auto pending = std::move(pending_issuance.record);
   const auto credential = pairing::approve_pending(
       pending, {.id = {.value = "credential-consumer"}, .now = now + std::chrono::seconds{2}});
   const auto pre_session_digest = pairing::identify_pre_session(pending_issuance.pre_session_token);
   const auto credential_binding = pairing::consume_approved_pre_session(pending, pending_issuance.pre_session_token,
                                                                         now + std::chrono::seconds{2});
   const auto exception_import = pairing::exceptions::code::replayed;

   return pending.state == pairing::pending_state::approved && credential.id.value == "credential-consumer" &&
                  pending.pre_session_consumed && pre_session_digest == pending.pre_session_digest &&
                  pending.approved_credential.has_value() && credential_binding == *pending.approved_credential &&
                  exception_import == pairing::exceptions::code::replayed
              ? 0
              : 1;
}
