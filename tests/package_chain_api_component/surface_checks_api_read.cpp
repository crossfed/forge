module;

#include <concepts>
#include <type_traits>

module package.chain_api_component.surface_checks;

import package.chain_api_component.test_support;
import forge.chain.api.authenticated_audit_verifier;
import forge.chain.api.block;
import forge.chain.api.finality;
import forge.chain.api.info;
import forge.chain.api.state;

namespace package_chain_api_component {

void check_read_api_surface() {
   namespace chain_api = forge::chain::api;

   static_assert(std::is_abstract_v<chain_api::info>);
   static_assert(std::is_abstract_v<chain_api::block>);
   static_assert(std::is_abstract_v<chain_api::state>);
   static_assert(std::derived_from<chain_api::authenticated_audit_verifier, chain_api::audit_verifier>);
   static_assert(std::is_abstract_v<chain_api::finality_verifier>);

   require(chain_api::state::ref().major == 3U, "installed state API does not expose version 3");
   require(chain_api::block::ref().major == 2U, "installed block API does not expose version 2");
   require(chain_api::info::ref().major == 2U, "installed info API does not expose version 2");
}

} // namespace package_chain_api_component
