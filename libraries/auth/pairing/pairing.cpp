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
   if (!scopes_subset_of(pending.requested_scopes, pending.scope_baseline) ||
       pending.expires_at <= pending.created_at) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pending pairing record is invalid");
   }
   if (pending.state == pending_state::pending && pending.resolved_at.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pending pairing record has an invalid resolution time");
   }
   if (pending.state != pending_state::pending && !pending.resolved_at.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "resolved pairing record has no resolution time");
   }
   if (pending.resolved_at.has_value() &&
       (*pending.resolved_at < pending.created_at || *pending.resolved_at >= pending.expires_at)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "resolved pairing record has an out-of-range resolution time");
   }
}

void require_pending(pending_request& pending, time_point now) {
   require_pending_record(pending);
   if (now < pending.created_at) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pairing transition time precedes pending request creation");
   }
   if (now >= pending.expires_at) {
      FORGE_THROW_EXCEPTION(exceptions::expired, "pending pairing request expired");
   }
   if (pending.state != pending_state::pending) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pairing request is not pending");
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

void verify_bootstrap_token(const bootstrap_record& bootstrap, const forge::crypto::core::secret_string& token) {
   if (token.size() != bootstrap_token_characters) {
      FORGE_THROW_EXCEPTION(exceptions::token_invalid, "bootstrap token is invalid");
   }

   auto decoded = forge::crypto::core::secret_bytes{};
   try {
      decoded.assign(forge::codec::base64::decode(token.view(), bootstrap_decode_options()));
   } catch (const forge::exceptions::base&) {
      FORGE_THROW_EXCEPTION(exceptions::token_invalid, "bootstrap token is invalid");
   }
   if (decoded.size() != bootstrap_token_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::token_invalid, "bootstrap token is invalid");
   }
   const auto canonical =
       forge::crypto::core::secret_string{forge::codec::base64::encode(decoded.span(), bootstrap_encode_options())};
   if (!forge::crypto::core::constant_time_equal(byte_view(token.view()), byte_view(canonical.view()))) {
      FORGE_THROW_EXCEPTION(exceptions::token_invalid, "bootstrap token is invalid");
   }

   const auto actual = forge::crypto::digest::sha256::hash(decoded.span());
   if (!forge::crypto::core::constant_time_equal(bootstrap.digest.value.to_uint8_span(), actual.to_uint8_span())) {
      FORGE_THROW_EXCEPTION(exceptions::token_invalid, "bootstrap token is invalid");
   }
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
   auto material = forge::crypto::core::secret_bytes{forge::crypto::core::random_bytes(bootstrap_token_bytes)};
   const auto digest = token_digest{.value = forge::crypto::digest::sha256::hash(material.span())};
   auto token =
       forge::crypto::core::secret_string{forge::codec::base64::encode(material.span(), bootstrap_encode_options())};

   return {
       .record =
           {
               .digest = digest,
               .scope_baseline = std::move(baseline),
               .created_at = options.now,
               .expires_at = options.expires_at,
           },
       .token = std::move(token),
   };
}

pending_request consume_bootstrap(bootstrap_record& bootstrap, const forge::crypto::core::secret_string& token,
                                  pairing_request request, consume_options options) {
   require_bootstrap_record(bootstrap);
   if (options.now < bootstrap.created_at) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pairing transition time precedes bootstrap creation");
   }
   verify_bootstrap_token(bootstrap, token);
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

   auto result = pending_request{
       .identity = std::move(request.identity),
       .requested_scopes = std::move(requested_scopes),
       .scope_baseline = bootstrap.scope_baseline,
       .created_at = options.now,
       .expires_at = options.request_expires_at,
   };
   bootstrap.consumed = true;
   return result;
}

pending_request supersede_pending(pending_request& pending, pairing_request replacement, time_point now) {
   require_pending(pending, now);
   require_identity(replacement.identity);
   auto requested_scopes = canonicalize_scopes(std::move(replacement.requested_scopes));
   if (!scopes_subset_of(requested_scopes, pending.scope_baseline)) {
      FORGE_THROW_EXCEPTION(exceptions::scope_invalid, "requested scopes exceed the bootstrap baseline");
   }
   if (replacement.identity == pending.identity && requested_scopes == pending.requested_scopes) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "pairing request has no replacement changes");
   }

   auto result = pending_request{
       .identity = std::move(replacement.identity),
       .requested_scopes = std::move(requested_scopes),
       .scope_baseline = pending.scope_baseline,
       .created_at = now,
       .expires_at = pending.expires_at,
   };
   pending.state = pending_state::superseded;
   pending.resolved_at = now;
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
   pending.state = pending_state::approved;
   pending.resolved_at = options.now;
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
