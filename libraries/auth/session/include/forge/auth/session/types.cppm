module;

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module forge.auth.session.types;

export import forge.auth.pairing.types;
export import forge.crypto.core.secret_string;
export import forge.crypto.digest.sha256;

export namespace forge::auth::session {

// Callers supply trusted, non-decreasing wall-clock values; persisted records retain them across restart.
using time_point = std::chrono::system_clock::time_point;

struct session_options {
   time_point now{};
   time_point absolute_expires_at{};
   std::chrono::system_clock::duration idle_timeout{};
};

enum class session_state {
   active,
   rotated,
   revoked,
};

struct session_record {
   forge::crypto::digest::sha256 session_digest;
   forge::crypto::digest::sha256 csrf_digest;
   forge::auth::pairing::credential_id credential_id;
   std::uint64_t credential_generation = 0;
   std::string identity;
   forge::auth::pairing::scope_set scopes;
   time_point created_at{};
   time_point last_activity_at{};
   time_point idle_expires_at{};
   time_point absolute_expires_at{};
   std::chrono::system_clock::duration idle_timeout{};
   session_state state = session_state::active;
   std::optional<time_point> terminal_at;

   bool operator==(const session_record&) const = default;
};

struct session_issuance {
   session_record record;
   forge::crypto::core::secret_string session_token;
   forge::crypto::core::secret_string csrf_secret;
};

struct principal {
   forge::auth::pairing::credential_id credential_id;
   std::uint64_t credential_generation = 0;
   std::string identity;
   forge::auth::pairing::scope_set scopes;

   bool operator==(const principal&) const = default;
};

} // namespace forge::auth::session
