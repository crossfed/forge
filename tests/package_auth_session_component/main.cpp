#include <chrono>

import forge.auth.session.exceptions;
import forge.auth.session.session;

int main() {
   namespace pairing = forge::auth::pairing;
   namespace session = forge::auth::session;

   const auto now = session::time_point{std::chrono::seconds{1}};
   const auto credential = pairing::credential{
       .id = {.value = "credential-consumer"},
       .identity = "consumer",
       .scopes = {"session.read"},
       .generation = 1,
       .issued_at = now,
       .updated_at = now,
   };
   auto issuance = session::issue_session(
       credential,
       {.now = now, .absolute_expires_at = now + std::chrono::minutes{1}, .idle_timeout = std::chrono::seconds{30}});
   session::validate_issuance(issuance);
   const auto principal =
       session::validate_session(issuance.record, issuance.session_token, credential, now + std::chrono::seconds{1});
   session::verify_csrf_secret(issuance.record, issuance.csrf_secret, now + std::chrono::seconds{1});
   const auto exception_import = session::exceptions::code::csrf_invalid;

   return principal.credential_id.value == "credential-consumer" &&
                  exception_import == session::exceptions::code::csrf_invalid
              ? 0
              : 1;
}
