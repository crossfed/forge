module;

#include <boost/describe.hpp>

#include <vector>

export module forge.chain.savanna.genesis;

export import forge.chain.protocol.block;
export import forge.chain.protocol.chain_config;
export import forge.chain.protocol.wasm_parameters;
export import forge.chain.savanna.policy_state;

export namespace forge::chain::savanna {

struct genesis {
   block_timestamp timestamp;
   forge::chain::protocol::public_key system_key;
   forge::chain::protocol::chain_config configuration;
   forge::chain::protocol::wasm_parameters wasm;
   forge::chain::protocol::producer_authority_schedule proposers;
   finalizer_policy finalizers;
   std::vector<forge::crypto::bls::signature> finalizer_proofs;
   std::vector<digest> protocol_features;
};

[[nodiscard]] forge::chain::protocol::chain_id calculate_chain_id(const genesis& value);
void validate(const genesis& value);

BOOST_DESCRIBE_STRUCT(genesis, (),
                      (timestamp, system_key, configuration, wasm, proposers, finalizers, finalizer_proofs,
                       protocol_features))

} // namespace forge::chain::savanna
