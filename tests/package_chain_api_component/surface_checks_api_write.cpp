module;

#include <type_traits>

module package.chain_api_component.surface_checks;

import package.chain_api_component.test_support;
import forge.chain.api.admin;
import forge.chain.api.submission;
import forge.chain.api.submission_client;
import forge.chain.api.transaction;

namespace package_chain_api_component {

void check_write_api_surface() {
   namespace chain_api = forge::chain::api;

   static_assert(std::is_abstract_v<chain_api::transaction>);
   static_assert(std::is_abstract_v<chain_api::submission>);
   static_assert(std::is_abstract_v<chain_api::admin>);
   static_assert(!std::is_abstract_v<chain_api::submission_client>);

   require(chain_api::admin::ref().major == 2U, "installed admin API does not expose version 2");
}

} // namespace package_chain_api_component
