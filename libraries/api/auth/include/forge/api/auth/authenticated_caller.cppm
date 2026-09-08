module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <utility>

export module forge.api.auth.authenticated_caller;

export import forge.api.core.server_supplied;
export import forge.crypto.digest.sha256;

import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.described;
import forge.variant.value;

export namespace forge::api::auth {

enum class caller_source : std::uint8_t {
   p2p_peer,
   tls_certificate,
};

// Canonical transport principal, populated only by a server-side binding.
struct authenticated_caller {
   authenticated_caller() = default;

   authenticated_caller(caller_source source_value, forge::crypto::digest::sha256 fingerprint_value)
       : source{source_value}, fingerprint{std::move(fingerprint_value)} {}

   caller_source source = caller_source::p2p_peer;
   forge::crypto::digest::sha256 fingerprint;

   [[nodiscard]] bool transport_authenticated() const noexcept {
      return transport_authenticated_;
   }

   bool operator==(const authenticated_caller& other) const {
      return source == other.source && fingerprint == other.fingerprint;
   }

 private:
   bool transport_authenticated_ = false;

   friend struct forge::api::core::server_supplied<authenticated_caller>;
};

BOOST_DESCRIBE_ENUM(caller_source, p2p_peer, tls_certificate)
BOOST_DESCRIBE_STRUCT(authenticated_caller, (), (source, fingerprint))

} // namespace forge::api::auth

export namespace forge::api::core {

template <> struct server_supplied<forge::api::auth::authenticated_caller> {
   static constexpr bool required = true;

   static void reset(forge::api::auth::authenticated_caller& value) {
      value = {};
   }

   [[nodiscard]] static bool apply(forge::api::auth::authenticated_caller& value, const trusted_invocation& trusted) {
      const auto* source = trusted.find<forge::api::auth::authenticated_caller>();
      if (source == nullptr) {
         return false;
      }
      value = *source;
      value.transport_authenticated_ = true;
      return true;
   }
};

} // namespace forge::api::core

FORGE_DECLARE_SERIALIZATION(forge::api::auth::authenticated_caller)
