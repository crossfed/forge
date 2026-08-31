module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

export module forge.chain.savanna.policy_state;

export import forge.chain.protocol.block;
export import forge.chain.protocol.producer_authority;
export import forge.chain.savanna.policy;
export import forge.crypto.bls;

export namespace forge::chain::savanna {

using block_num = forge::chain::protocol::block_num;
using block_timestamp = forge::chain::protocol::block_timestamp;

struct finalizer_policy_state {
   finalizer_policy policy;
   std::vector<forge::crypto::bls::signature> proofs;

   // Operational cache only; raw bytes remain the protocol policy and proofs.
   mutable digest verified_source;
   mutable std::optional<verified_finalizer_policy> verified;
};

struct proposer_policy {
   block_timestamp proposal_time;
   forge::chain::protocol::producer_authority_schedule proposers;
};

struct proposer_policy_diff {
   std::uint32_t version = 0;
   block_timestamp proposal_time;
   ordered_diff<forge::chain::protocol::producer_authority> producers;
};

[[nodiscard]] verified_finalizer_policy validate(const finalizer_policy_state& value);
[[nodiscard]] finalizer_policy_state apply(const finalizer_policy_state& source,
                                           const finalizer_policy_diff& difference,
                                           std::span<const forge::crypto::bls::signature> inserted_proofs);
[[nodiscard]] proposer_policy apply(const proposer_policy& source, const proposer_policy_diff& difference);
[[nodiscard]] finalizer_policy_diff difference(const finalizer_policy_state& source, const finalizer_policy& target);
[[nodiscard]] proposer_policy_diff difference(const proposer_policy& source,
                                              const forge::chain::protocol::producer_authority_schedule& target,
                                              block_timestamp proposal_time);

BOOST_DESCRIBE_STRUCT(finalizer_policy_state, (), (policy, proofs))
BOOST_DESCRIBE_STRUCT(proposer_policy, (), (proposal_time, proposers))
BOOST_DESCRIBE_STRUCT(proposer_policy_diff, (), (version, proposal_time, producers))

} // namespace forge::chain::savanna
