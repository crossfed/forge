module;

#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <exception>
#include <map>
#include <string>
#include <utility>

module forge.plugins.crypto.signer.plugin;

import forge.config.core.component;
import forge.config.core.decode;
import forge.crypto.asymmetric;
import forge.crypto.bls;
import forge.exceptions;
import forge.plugins.crypto.signer.exceptions;
import forge.plugins.crypto.signer.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"

namespace forge::plugins::crypto::signer {

config decode_config(const forge::config::core::component_view& view) {
   if (view.source().try_get(std::string{view.section()} + ".default-output-profile") != nullptr) {
      FORGE_THROW_EXCEPTION(
          exceptions::invalid_config,
          "crypto signer default-output-profile was removed; format typed results at the consumer boundary");
   }
   auto decoded = forge::config::core::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, forge::config::core::format_decode_diagnostics(
                                                            "invalid crypto signer config", decoded.diagnostics));
   }
   return std::move(decoded.value);
}

void apply_config(plugin::impl& state, forge::config::core::component_view view) {
   auto config = decode_config(view);
   auto loaded = std::map<std::string, plugin::impl::loaded_key>{};
   for (auto& key : config.keys) {
      const auto& input_profile = state.profile_by_name(key.input_profile);
      auto private_key = forge::crypto::asymmetric::private_key{};
      try {
         private_key = input_profile.parse_private(key.private_key);
      } catch (const std::exception&) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_key, "crypto signer private key is invalid",
                               forge::exceptions::ctx("key_id", key.id),
                               forge::exceptions::ctx("input_profile", key.input_profile));
      }
      loaded.emplace(key.id, plugin::impl::loaded_key{
                                 .key_id = key.id,
                                 .private_key = std::move(private_key),
                                 .purposes = std::move(key.purposes),
                             });
   }
   auto loaded_bls = std::map<std::string, plugin::impl::loaded_bls_key>{};
   for (auto& key : config.bls_keys) {
      try {
         auto private_key = forge::crypto::bls::encoding::parse_private_key(key.private_key);
         loaded_bls.emplace(key.id, plugin::impl::loaded_bls_key{
                                        .key_id = key.id,
                                        .private_key = std::move(private_key),
                                        .purposes = std::move(key.purposes),
                                    });
      } catch (const std::exception&) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_key, "crypto signer BLS private key is invalid",
                               forge::exceptions::ctx("key_id", key.id));
      }
   }
   state.keys = std::move(loaded);
   state.bls_keys = std::move(loaded_bls);
   state.stopping.store(false, std::memory_order_release);
}

} // namespace forge::plugins::crypto::signer
