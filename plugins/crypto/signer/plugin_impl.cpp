module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.plugins.crypto.signer.plugin;

import forge.crypto.asymmetric;
import forge.crypto.bls;
import forge.crypto.digest.sha256;
import forge.exceptions;
import forge.plugins.crypto.signer.api;
import forge.plugins.crypto.signer.bls_api;
import forge.plugins.crypto.signer.exceptions;
import forge.plugins.crypto.signer.types;

#include "details/plugin_impl.hxx"

namespace forge::plugins::crypto::signer {
namespace {

[[nodiscard]] bool purpose_allowed(const std::vector<std::string>& allowed, std::string_view value) noexcept {
   return std::ranges::find_if(allowed, [value](const auto& purpose) { return purpose == value; }) != allowed.end();
}

} // namespace

plugin::impl::impl(plugin_options options) : profiles{make_profiles(std::move(options))} {}

plugin::impl::profile_map plugin::impl::make_profiles(plugin_options options) {
   auto result = profile_map{};
   auto add_profile = [&](const forge::crypto::asymmetric::text_encoding_profile& profile) {
      auto encoding = forge::crypto::asymmetric::encoding::from_profile(profile);
      const auto id = encoding.id();
      if (!result.emplace(id, std::move(encoding)).second) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "signer encoding profile id is duplicated",
                               forge::exceptions::ctx("profile", id));
      }
   };

   add_profile(forge::crypto::asymmetric::profiles::forge());
   add_profile(forge::crypto::asymmetric::profiles::antelope());
   result.emplace("eos", forge::crypto::asymmetric::encoding::antelope());
   add_profile(forge::crypto::asymmetric::profiles::bitcoin());
   add_profile(forge::crypto::asymmetric::profiles::solana());
   add_profile(forge::crypto::asymmetric::profiles::tezos());
   for (const auto& profile : options.profiles) {
      add_profile(profile);
   }
   return result;
}

const forge::crypto::asymmetric::encoding& plugin::impl::profile_by_name(std::string_view value) const {
   const auto found = profiles.find(std::string{value});
   if (found == profiles.end()) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_profile, "unknown signer encoding profile",
                            forge::exceptions::ctx("profile", std::string{value}));
   }
   return found->second;
}

response plugin::impl::sign(request value) const {
   require_available();

   const auto found = keys.find(value.key_id);
   if (found == keys.end()) {
      FORGE_THROW_EXCEPTION(exceptions::key_not_found, "signer key is not configured",
                            forge::exceptions::ctx("key_id", value.key_id));
   }

   const auto& key = found->second;
   if (!purpose_allowed(key.purposes, value.purpose)) {
      FORGE_THROW_EXCEPTION(exceptions::purpose_denied, "signer key is not allowed for requested purpose",
                            forge::exceptions::ctx("key_id", value.key_id),
                            forge::exceptions::ctx("purpose", value.purpose));
   }

   if (value.required_algorithm && *value.required_algorithm != key.private_key.type()) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_algorithm, "signer key algorithm does not match request");
   }

   return response{
       .key_id = key.key_id,
       .public_key = key.private_key.get_public_key(),
       .signature = key.private_key.sign_digest(value.digest),
   };
}

bls_description plugin::impl::describe(bls_describe_request value) const {
   require_available();

   const auto found = bls_keys.find(value.key_id);
   if (found == bls_keys.end()) {
      FORGE_THROW_EXCEPTION(exceptions::key_not_found, "signer BLS key is not configured",
                            forge::exceptions::ctx("key_id", value.key_id));
   }

   const auto& key = found->second;
   if (!purpose_allowed(key.purposes, value.purpose)) {
      FORGE_THROW_EXCEPTION(exceptions::purpose_denied, "signer BLS key is not allowed for requested purpose",
                            forge::exceptions::ctx("key_id", value.key_id),
                            forge::exceptions::ctx("purpose", value.purpose));
   }

   return bls_description{
       .key_id = key.key_id,
       .public_key = key.private_key.get_public_key(),
       .proof_of_possession = key.private_key.proof_of_possession(),
   };
}

bls_response plugin::impl::sign(bls_sign_request value) const {
   require_available();

   const auto found = bls_keys.find(value.key_id);
   if (found == bls_keys.end()) {
      FORGE_THROW_EXCEPTION(exceptions::key_not_found, "signer BLS key is not configured",
                            forge::exceptions::ctx("key_id", value.key_id));
   }

   const auto& key = found->second;
   if (!purpose_allowed(key.purposes, value.purpose)) {
      FORGE_THROW_EXCEPTION(exceptions::purpose_denied, "signer BLS key is not allowed for requested purpose",
                            forge::exceptions::ctx("key_id", value.key_id),
                            forge::exceptions::ctx("purpose", value.purpose));
   }

   return bls_response{
       .key_id = key.key_id,
       .public_key = key.private_key.get_public_key(),
       .signature = key.private_key.sign(value.message),
   };
}

void plugin::impl::require_available() const {
   if (stopping.load(std::memory_order_acquire)) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "crypto signer is unavailable");
   }
}

} // namespace forge::plugins::crypto::signer
