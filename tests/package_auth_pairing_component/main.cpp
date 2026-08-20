#include <chrono>

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
   auto pending = pairing::consume_bootstrap(issuance.record, issuance.token,
                                             {.identity = "consumer", .requested_scopes = {"pair.read"}},
                                             {.now = now + std::chrono::seconds{1},
                                              .request_expires_at = now + std::chrono::seconds{30},
                                              .pending_count = 0,
                                              .max_pending_requests = 1});
   const auto credential = pairing::approve_pending(
       pending, {.id = {.value = "credential-consumer"}, .now = now + std::chrono::seconds{2}});
   const auto exception_import = pairing::exceptions::code::replayed;

   return pending.state == pairing::pending_state::approved && credential.id.value == "credential-consumer" &&
                  exception_import == pairing::exceptions::code::replayed
              ? 0
              : 1;
}
