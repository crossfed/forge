module;

#include <forge/exceptions/macros.hpp>

#include <utility>

module forge.plugins.chain.signer.plugin;

import forge.config.core.component;
import forge.config.core.decode;
import forge.plugins.chain.signer.exceptions;
import forge.plugins.chain.signer.types;

#include "details/config.hxx"

namespace forge::plugins::chain::signer {

config decode_config(const forge::config::core::component_view& view) {
   auto decoded = forge::config::core::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, forge::config::core::format_decode_diagnostics(
                                                            "invalid Chain signer config", decoded.diagnostics));
   }
   return std::move(decoded.value);
}

} // namespace forge::plugins::chain::signer
