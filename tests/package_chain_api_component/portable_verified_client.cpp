#include <concepts>
#include <memory>
#include <span>
#include <string>
#include <vector>

import forge.chain.api.contract_table_projection_verifier;
import forge.chain.api.savanna_finality_verifier;
import forge.chain.api.verified_client_factory;

bool portable_verified_client_package_contract() {
   namespace api = forge::chain::api;
   static_assert(std::same_as<decltype(&api::make_verified_client),
                              api::verified_client (*)(api::raw_client, api::verified_client_options)>);
   static_assert(
       std::same_as<decltype(api::verified_client_options::finality), std::shared_ptr<api::finality_verifier>>);
   using single_trust_factory = std::shared_ptr<api::savanna_finality_verifier> (*)(
       forge::chain::savanna::finality_trust, forge::chain::savanna::finality_witness_limits);
   using multi_trust_factory = std::shared_ptr<api::savanna_finality_verifier> (*)(
       forge::chain::savanna::finality_trust, std::vector<forge::chain::savanna::finality_trust>,
       forge::chain::savanna::finality_witness_limits);
   using api::make_savanna_finality_verifier;
   static_assert(std::same_as<decltype(&make_savanna_finality_verifier), single_trust_factory>);
   static_assert(std::same_as<decltype(&api::make_savanna_finality_verifier_with_trusts), multi_trust_factory>);
   static_assert(requires {
      api::verified_client_options{
          forge::chain::protocol::chain_id{},        std::string{},
          std::shared_ptr<api::finality_verifier>{}, forge::db::authenticated::limits{},
          forge::chain::protocol::service_limits{},  std::shared_ptr<api::projection_verifier>{},
      };
   });
   static_assert(std::same_as<decltype(api::make_contract_table_projection_verifier()),
                              std::shared_ptr<api::projection_verifier>>);
   return static_cast<bool>(api::make_contract_table_projection_verifier());
}
