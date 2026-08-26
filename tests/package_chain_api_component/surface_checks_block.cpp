module;

#include <concepts>
#include <optional>
#include <type_traits>
#include <vector>

module package.chain_api_component.surface_checks;

import forge.chain.protocol.activated_protocol_feature_info;
import forge.chain.protocol.block_query;
import forge.chain.protocol.chain_config;
import forge.chain.protocol.finalizer_vote_record;
import forge.chain.protocol.float64;
import forge.chain.protocol.producer_info;
import forge.chain.protocol.protocol_feature;
import forge.chain.protocol.wasm_parameters;

namespace package_chain_api_component {

void check_block_surface() {
   namespace protocol = forge::chain::protocol;

   static_assert(std::is_same_v<decltype(protocol::protocol_features_response{}.features),
                                std::vector<protocol::activated_protocol_feature_info>>);
   static_assert(std::derived_from<protocol::activated_protocol_feature_info, protocol::protocol_feature>);
   static_assert(
       std::is_same_v<decltype(protocol::consensus_parameters_response{}.parameters), protocol::chain_config>);
   static_assert(std::is_same_v<decltype(protocol::consensus_parameters_response{}.wasm),
                                std::optional<protocol::wasm_parameters>>);
   static_assert(
       std::is_same_v<decltype(protocol::producers_request{}.lower_bound), std::optional<protocol::account_name>>);
   static_assert(std::is_same_v<decltype(protocol::producers_request{}.cursor), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::producers_response{}.rows), std::vector<protocol::producer_info>>);
   static_assert(std::is_same_v<decltype(protocol::producers_response{}.total_vote_weight), protocol::float64>);
   static_assert(std::is_same_v<decltype(protocol::producers_response{}.next), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::finalizer_info_response{}.last_votes),
                                std::vector<protocol::finalizer_vote_record>>);
}

} // namespace package_chain_api_component
