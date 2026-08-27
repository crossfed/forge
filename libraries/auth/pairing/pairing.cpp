module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

module forge.auth.pairing.pairing;

import forge.codec.base64;
import forge.crypto.core.constant_time;
import forge.crypto.core.random;
import forge.crypto.core.secret_bytes;
import forge.crypto.core.secret_string;
import forge.crypto.digest.sha256;
import forge.exceptions;

namespace forge::auth::pairing {
namespace {

constexpr auto bootstrap_token_bytes = std::size_t{32};
constexpr auto bootstrap_token_characters = std::size_t{43};

struct token_issuance {
   token_digest digest;
   forge::crypto::core::secret_string token;
};

static_assert(std::is_nothrow_move_constructible_v<credential_binding>);
static_assert(std::is_nothrow_move_assignable_v<pending_request>);

[[nodiscard]] std::span<const std::uint8_t> byte_view(std::string_view value) {
   return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}

[[nodiscard]] forge::codec::base64::encode_options bootstrap_encode_options() {
   return {
       .characters = forge::codec::base64::alphabet::url,
       .pad = forge::codec::base64::padding::omit,
   };
}

[[nodiscard]] forge::codec::base64::decode_options bootstrap_decode_options() {
   return {
       .characters = forge::codec::base64::alphabet::url,
       .pad = forge::codec::base64::padding_policy::forbid,
       .ignore_ascii_whitespace = false,
   };
}

void require_identity(std::string_view identity) {
   if (identity.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::identity_invalid, "pairing identity must not be empty");
   }
}

void require_credential_id(const credential_id& id) {
   if (id.value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::credential_id_invalid, "credential ID must not be empty");
   }
}

void require_canonical_scopes(const scope_set& scopes) {
   if (scopes.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::scope_invalid, "pairing scopes must not be empty");
   }
   for (const auto& scope : scopes) {
      if (scope.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::scope_invalid, "pairing scope must not be empty");
      }
   }
   if (!std::is_sorted(scopes.begin(), scopes.end()) ||
       std::adjacent_find(scopes.begin(), scopes.end()) != scopes.end()) {
      FORGE_THROW_EXCEPTION(exceptions::scope_invalid, "pairing scopes must be canonical");
   }
}

void require_future_expiry(time_point now, time_point expires_at, std::string_view subject) {
   if (expires_at <= now) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "pairing expiry must be after now",
                            forge::exceptions::ctx("subject", subject));
   }
}

[[nodiscard]] bool scopes_subset_of(const scope_set& requested, const scope_set& baseline) {
   return std::includes(baseline.begin(), baseline.end(), requested.begin(), requested.end());
}

void require_bootstrap_record(const bootstrap_record& bootstrap) {
   require_canonical_scopes(bootstrap.scope_baseline);
   if (bootstrap.expires_at <= bootstrap.created_at || bootstrap.digest.value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "bootstrap record is invalid");
   }
}

void require_pending_record(const pending_request& pending) {
   require_identity(pending.identity);
   require_canonical_scopes(pending.scope_baseline);
   require_canonical_scopes(pending.requested_scopes);
   if (pending.pre_session_digest.value.empty() ||
       !scopes_subset_of(pending.requested_scopes, pending.scope_baseline) ||
       pending.expires_at <= pending.created_at) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pending pairing record is invalid");
   }
   switch (pending.state) {
   case pending_state::pending:
      if (pending.resolved_at.has_value() || pending.approved_credential.has_value() || pending.pre_session_consumed) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pending pairing record has an invalid resolution time");
      }
      break;
   case pending_state::approved:
      if (!pending.resolved_at.has_value() || !pending.approved_credential.has_value() ||
          pending.approved_credential->id.value.empty() || pending.approved_credential->generation != 1) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_state, "resolved pairing record has no resolution time");
      }
      break;
   case pending_state::rejected:
   case pending_state::superseded:
      if (!pending.resolved_at.has_value() || pending.approved_credential.has_value() || pending.pre_session_consumed) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_state, "resolved pairing record has no resolution time");
      }
      break;
   default:
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pending pairing record has an unknown state");
   }
   if (pending.resolved_at.has_value() &&
       (*pending.resolved_at < pending.created_at || *pending.resolved_at >= pending.expires_at)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "resolved pairing record has an out-of-range resolution time");
   }
}

void require_pending_time(const pending_request& pending, time_point now) {
   require_pending_record(pending);
   if (now < pending.created_at || (pending.resolved_at.has_value() && now < *pending.resolved_at)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pairing transition time precedes pending request creation");
   }
   if (now >= pending.expires_at) {
      FORGE_THROW_EXCEPTION(exceptions::expired, "pending pairing request expired");
   }
}

void require_pending(const pending_request& pending, time_point now) {
   require_pending_time(pending, now);
   if (pending.state != pending_state::pending) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pairing request is not pending");
   }
}

void require_pre_session_lifecycle(const pending_request& pending, time_point now) {
   if (now < pending.created_at || (pending.resolved_at.has_value() && now < *pending.resolved_at)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pairing transition time precedes pending request creation");
   }
   if (pending.pre_session_consumed) {
      FORGE_THROW_EXCEPTION(exceptions::replayed, "pre-session token was already consumed");
   }
   if (now >= pending.expires_at) {
      FORGE_THROW_EXCEPTION(exceptions::expired, "pending pairing request expired");
   }
}

void require_credential_record(const credential& value) {
   require_credential_id(value.id);
   require_identity(value.identity);
   require_canonical_scopes(value.scopes);
   if (value.generation == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "credential record is invalid");
   }
   if (value.state == credential_state::active && value.revoked_at.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "active credential has a revocation time");
   }
   if (value.state == credential_state::revoked) {
      if (!value.revoked_at.has_value()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_state, "revoked credential has no revocation time");
      }
      if (*value.revoked_at != value.updated_at || *value.revoked_at < value.issued_at) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_state, "revoked credential has an invalid revocation time");
      }
   }
   if (value.updated_at < value.issued_at) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "credential record update precedes issuance");
   }
}

void require_active_credential(credential& value, time_point now) {
   require_credential_record(value);
   if (now < value.updated_at || value.state != credential_state::active) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "credential is not active");
   }
}

[[nodiscard]] forge::crypto::core::secret_bytes
decode_canonical_token(const forge::crypto::core::secret_string& token) {
   if (token.size() != bootstrap_token_characters) {
      FORGE_THROW_EXCEPTION(exceptions::token_invalid, "pairing token is invalid");
   }

   auto decoded = forge::crypto::core::secret_bytes{};
   try {
      decoded.assign(forge::codec::base64::decode(token.view(), bootstrap_decode_options()));
   } catch (const forge::exceptions::base&) {
      FORGE_THROW_EXCEPTION(exceptions::token_invalid, "pairing token is invalid");
   }
   if (decoded.size() != bootstrap_token_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::token_invalid, "pairing token is invalid");
   }
   const auto canonical =
       forge::crypto::core::secret_string{forge::codec::base64::encode(decoded.span(), bootstrap_encode_options())};
   if (!forge::crypto::core::constant_time_equal(byte_view(token.view()), byte_view(canonical.view()))) {
      FORGE_THROW_EXCEPTION(exceptions::token_invalid, "pairing token is invalid");
   }

   return decoded;
}

[[nodiscard]] token_digest identify_token(const forge::crypto::core::secret_string& token) {
   auto decoded = decode_canonical_token(token);
   return {.value = forge::crypto::digest::sha256::hash(decoded.span())};
}

void verify_token(const token_digest& expected, const forge::crypto::core::secret_string& token) {
   const auto actual = identify_token(token);
   if (!forge::crypto::core::constant_time_equal(expected.value.to_uint8_span(), actual.value.to_uint8_span())) {
      FORGE_THROW_EXCEPTION(exceptions::token_invalid, "pairing token is invalid");
   }
}

[[nodiscard]] token_issuance issue_token() {
   auto material = forge::crypto::core::secret_bytes{forge::crypto::core::random_bytes(bootstrap_token_bytes)};
   return {
       .digest = {.value = forge::crypto::digest::sha256::hash(material.span())},
       .token = forge::crypto::core::secret_string{forge::codec::base64::encode(material.span(),
                                                                                bootstrap_encode_options())},
   };
}

[[nodiscard]] token_issuance issue_distinct_token(const token_digest& forbidden) {
   auto result = issue_token();
   if (forge::crypto::core::constant_time_equal(result.digest.value.to_uint8_span(), forbidden.value.to_uint8_span())) {
      FORGE_THROW_EXCEPTION(exceptions::token_collision, "pairing token material collided with an existing digest");
   }
   return result;
}

} // namespace

scope_set canonicalize_scopes(scope_set scopes) {
   for (const auto& scope : scopes) {
      if (scope.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::scope_invalid, "pairing scope must not be empty");
      }
   }
   std::ranges::sort(scopes);
   scopes.erase(std::unique(scopes.begin(), scopes.end()), scopes.end());
   require_canonical_scopes(scopes);
   return scopes;
}

bootstrap_issuance begin_bootstrap(bootstrap_options options) {
   require_future_expiry(options.now, options.expires_at, "bootstrap");
   auto baseline = canonicalize_scopes(std::move(options.scope_baseline));
   auto issuance = issue_token();

   return {
       .record =
           {
               .digest = issuance.digest,
               .scope_baseline = std::move(baseline),
               .created_at = options.now,
               .expires_at = options.expires_at,
           },
       .token = std::move(issuance.token),
   };
}

token_digest identify_bootstrap_token(const forge::crypto::core::secret_string& bootstrap_token) {
   return identify_token(bootstrap_token);
}

pending_issuance consume_bootstrap(bootstrap_record& bootstrap, const forge::crypto::core::secret_string& token,
                                   pairing_request request, consume_options options) {
   require_bootstrap_record(bootstrap);
   if (options.now < bootstrap.created_at) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pairing transition time precedes bootstrap creation");
   }
   verify_token(bootstrap.digest, token);
   if (bootstrap.consumed) {
      FORGE_THROW_EXCEPTION(exceptions::replayed, "bootstrap token was already consumed");
   }
   if (options.now >= bootstrap.expires_at) {
      FORGE_THROW_EXCEPTION(exceptions::expired, "bootstrap token expired");
   }
   if (options.max_pending_requests == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "pending request capacity must be positive");
   }
   if (options.pending_count >= options.max_pending_requests) {
      FORGE_THROW_EXCEPTION(exceptions::capacity_exceeded, "pending request capacity exceeded");
   }
   require_future_expiry(options.now, options.request_expires_at, "pending request");
   require_identity(request.identity);
   auto requested_scopes = canonicalize_scopes(std::move(request.requested_scopes));
   if (!scopes_subset_of(requested_scopes, bootstrap.scope_baseline)) {
      FORGE_THROW_EXCEPTION(exceptions::scope_invalid, "requested scopes exceed the bootstrap baseline");
   }

   auto pre_session = issue_distinct_token(bootstrap.digest);
   auto result = pending_issuance{
       .record =
           {
               .identity = std::move(request.identity),
               .requested_scopes = std::move(requested_scopes),
               .scope_baseline = bootstrap.scope_baseline,
               .pre_session_digest = pre_session.digest,
               .created_at = options.now,
               .expires_at = options.request_expires_at,
           },
       .pre_session_token = std::move(pre_session.token),
   };
   bootstrap.consumed = true;
   return result;
}

pending_issuance supersede_pending(pending_request& pending, pairing_request replacement, time_point now) {
   require_pending(pending, now);
   require_identity(replacement.identity);
   auto requested_scopes = canonicalize_scopes(std::move(replacement.requested_scopes));
   if (!scopes_subset_of(requested_scopes, pending.scope_baseline)) {
      FORGE_THROW_EXCEPTION(exceptions::scope_invalid, "requested scopes exceed the bootstrap baseline");
   }
   if (replacement.identity == pending.identity && requested_scopes == pending.requested_scopes) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pairing request has no replacement changes");
   }

   auto pre_session = issue_distinct_token(pending.pre_session_digest);
   auto result = pending_issuance{
       .record =
           {
               .identity = std::move(replacement.identity),
               .requested_scopes = std::move(requested_scopes),
               .scope_baseline = pending.scope_baseline,
               .pre_session_digest = pre_session.digest,
               .created_at = now,
               .expires_at = pending.expires_at,
           },
       .pre_session_token = std::move(pre_session.token),
   };
   pending.state = pending_state::superseded;
   pending.resolved_at = now;
   return result;
}

token_digest identify_pre_session(const forge::crypto::core::secret_string& pre_session_token) {
   return identify_token(pre_session_token);
}

void validate_pre_session(const pending_request& pending, const forge::crypto::core::secret_string& pre_session_token,
                          time_point now) {
   require_pending_record(pending);
   verify_token(pending.pre_session_digest, pre_session_token);
   require_pre_session_lifecycle(pending, now);
}

credential_binding consume_approved_pre_session(pending_request& pending,
                                                const forge::crypto::core::secret_string& pre_session_token,
                                                time_point now) {
   require_pending_record(pending);
   verify_token(pending.pre_session_digest, pre_session_token);
   require_pre_session_lifecycle(pending, now);
   if (pending.state != pending_state::approved) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pre-session token cannot exchange an unapproved request");
   }
   auto result = *pending.approved_credential;
   pending.pre_session_consumed = true;
   return result;
}

credential approve_pending(pending_request& pending, approval_options options) {
   require_pending(pending, options.now);
   require_credential_id(options.id);
   auto result = credential{
       .id = std::move(options.id),
       .identity = pending.identity,
       .scopes = pending.requested_scopes,
       .generation = 1,
       .issued_at = options.now,
       .updated_at = options.now,
   };
   auto binding = credential_binding{
       .id = result.id,
       .generation = result.generation,
   };
   auto approved_pending = pending;
   approved_pending.approved_credential.emplace(std::move(binding));
   approved_pending.state = pending_state::approved;
   approved_pending.resolved_at = options.now;
   pending = std::move(approved_pending);
   return result;
}

void reject_pending(pending_request& pending, time_point now) {
   require_pending(pending, now);
   pending.state = pending_state::rejected;
   pending.resolved_at = now;
}

void rotate_credential_downscope(credential& value, scope_set scopes, time_point now) {
   require_active_credential(value, now);
   scopes = canonicalize_scopes(std::move(scopes));
   if (!scopes_subset_of(scopes, value.scopes)) {
      FORGE_THROW_EXCEPTION(exceptions::scope_invalid, "credential rotation cannot escalate scopes");
   }
   if (scopes == value.scopes) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "credential rotation must reduce scopes");
   }
   if (value.generation == std::numeric_limits<std::uint64_t>::max()) {
      FORGE_THROW_EXCEPTION(exceptions::generation_exhausted, "credential generation is exhausted");
   }

   value.scopes = std::move(scopes);
   ++value.generation;
   value.updated_at = now;
}

void revoke_credential(credential& value, time_point now) {
   require_active_credential(value, now);
   value.state = credential_state::revoked;
   value.updated_at = now;
   value.revoked_at = now;
}

} // namespace forge::auth::pairing
