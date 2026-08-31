#include <concepts>
#include <memory>
#include <span>
#include <string>
#include <vector>

import forge.chain.api.contract_table_projection_verifier;
import forge.chain.api.verified_client_factory;

bool portable_verified_client_package_contract() {
   namespace api = forge::chain::api;
   static_assert(std::same_as<decltype(&api::make_verified_client),
                              api::verified_client (*)(api::raw_client, api::verified_client_options)>);
   static_assert(std::same_as<decltype(api::verified_client_options::trust),
                              forge::chain::savanna::finality_trust>);
   static_assert(std::same_as<decltype(api::verified_client_options::additional_trusts),
                              std::vector<forge::chain::savanna::finality_trust>>);
   using single_trust_factory = std::shared_ptr<api::finality_verifier> (*)
       (forge::chain::savanna::finality_trust, forge::chain::savanna::finality_witness_limits);
   using multi_trust_factory = std::shared_ptr<api::finality_verifier> (*)
       (forge::chain::savanna::finality_trust, std::vector<forge::chain::savanna::finality_trust>,
        forge::chain::savanna::finality_witness_limits);
   using api::make_savanna_finality_verifier;
   static_assert(std::same_as<decltype(&make_savanna_finality_verifier), single_trust_factory>);
   static_assert(std::same_as<decltype(&api::make_savanna_finality_verifier_with_trusts), multi_trust_factory>);
   static_assert(requires {
      api::verified_client_options{
          forge::chain::protocol::chain_id{},
          std::string{},
          forge::chain::savanna::finality_trust{},
          forge::db::authenticated::limits{},
          forge::chain::protocol::service_limits{},
          std::shared_ptr<api::projection_verifier>{},
      };
   });
   static_assert(std::same_as<decltype(&api::replay_savanna_finality_state),
                              forge::chain::savanna::header_state (*)
                                  (std::span<const forge::chain::savanna::finality_trust>,
                                   const forge::chain::protocol::state_anchor&,
                                   const forge::chain::protocol::proof_blob&,
                                   forge::chain::savanna::finality_witness_limits)>);
   static_assert(std::same_as<decltype(api::make_contract_table_projection_verifier()),
                              std::shared_ptr<api::projection_verifier>>);
   return static_cast<bool>(api::make_contract_table_projection_verifier());
}
