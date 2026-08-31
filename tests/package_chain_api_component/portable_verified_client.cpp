#include <concepts>
#include <memory>

import forge.chain.api.contract_table_projection_verifier;
import forge.chain.api.verified_client_factory;

bool portable_verified_client_package_contract() {
   namespace api = forge::chain::api;
   static_assert(std::same_as<decltype(&api::make_verified_client),
                              api::verified_client (*)(api::raw_client, api::verified_client_options)>);
   static_assert(std::same_as<decltype(api::make_contract_table_projection_verifier()),
                              std::shared_ptr<api::projection_verifier>>);
   return static_cast<bool>(api::make_contract_table_projection_verifier());
}
