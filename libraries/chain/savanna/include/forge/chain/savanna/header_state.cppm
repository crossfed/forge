module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

export module forge.chain.savanna.header_state;

export import forge.chain.savanna.extensions;
export import forge.chain.savanna.genesis;
export import forge.chain.savanna.finality_core;

export namespace forge::chain::savanna {

struct header_state {
   block_id id;
   forge::chain::protocol::block_header header;
   std::vector<digest> activated_protocol_features;
   forge::chain::savanna::block_ref block;
   forge::chain::savanna::finality_core finality;
   finalizer_policy_state active_finalizers;
   std::vector<std::pair<block_num, finalizer_policy_state>> proposed_finalizers;
   std::optional<std::pair<block_num, finalizer_policy_state>> pending_finalizers;
   std::optional<finalizer_policy_state> latest_qc_finalizers;
   std::uint32_t finalizer_generation = 1;
   digest last_pending_finalizer_digest;
   block_timestamp last_pending_finalizer_start;
   proposer_policy active_proposers;
   std::optional<proposer_policy> proposed_proposers;
   std::optional<proposer_policy> pending_proposers;

   [[nodiscard]] block_num num() const;
   [[nodiscard]] digest base_digest() const;
   [[nodiscard]] digest finality_digest() const;
   [[nodiscard]] forge::chain::savanna::block_ref make_block_ref() const;
   [[nodiscard]] const finalizer_policy_state& last_proposed_finalizers() const;
   [[nodiscard]] const finalizer_policy_state& last_pending_finalizers() const;
   [[nodiscard]] const proposer_policy& last_proposed_proposers() const;
};

[[nodiscard]] header_state make_genesis_state(const genesis& value, const forge::chain::protocol::signed_block& block);
[[nodiscard]] header_state transition(const header_state& previous,
                                      const forge::chain::protocol::signed_block_header& header,
                                      const header_extensions& extensions);
[[nodiscard]] const forge::chain::protocol::producer_authority& scheduled_producer(const header_state& previous,
                                                                                   block_timestamp timestamp);
[[nodiscard]] std::pair<forge::chain::savanna::verified_finalizer_policy,
                        std::optional<forge::chain::savanna::verified_finalizer_policy>>
finalizer_policies(const header_state& state, block_num block);
[[nodiscard]] std::pair<forge::chain::savanna::verified_finalizer_policy,
                        std::optional<forge::chain::savanna::verified_finalizer_policy>>
current_finalizer_policies(const header_state& state);
[[nodiscard]] const finalizer_policy_state& finalizer_policy_for(const header_state& state, std::uint32_t generation);

BOOST_DESCRIBE_STRUCT(header_state, (),
                      (id, header, activated_protocol_features, block, finality, active_finalizers, proposed_finalizers,
                       pending_finalizers, latest_qc_finalizers, finalizer_generation, last_pending_finalizer_digest,
                       last_pending_finalizer_start, active_proposers, proposed_proposers, pending_proposers))

} // namespace forge::chain::savanna
