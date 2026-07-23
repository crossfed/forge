module;

#include <boost/describe.hpp>

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

export module forge.chain.savanna.types;

export import forge.chain.core.types;
export import forge.crypto.bls;
export import forge.variant.dynamic_bitset;

export namespace forge::chain::savanna {

using block_num_t = std::uint32_t;
using block_slot_t = std::uint32_t;
using block_id = forge::chain::core::digest;
using digest = forge::chain::core::digest;
using vote_bitset = forge::dynamic_bitset;

struct finalizer {
   std::string description;
   std::uint64_t weight = 0;
   forge::crypto::bls::public_key public_key;

   auto operator<=>(const finalizer&) const = default;
};

struct finalizer_policy {
   std::uint32_t generation = 0;
   std::uint64_t threshold = 0;
   std::vector<finalizer> finalizers;

   auto operator<=>(const finalizer_policy&) const = default;
};

struct block_ref {
   block_num_t num = 0;
   block_id id;
   block_slot_t slot = 0;
   digest finality_digest;
   std::uint32_t active_policy_generation = 0;
   std::uint32_t pending_policy_generation = 0;

   [[nodiscard]] bool empty() const noexcept;

   auto operator<=>(const block_ref&) const = default;
};

struct block_ref_digest_data {
   block_num_t num = 0;
   block_slot_t slot = 0;
   digest finality_digest;
   block_slot_t parent_slot = 0;
};

BOOST_DESCRIBE_STRUCT(finalizer, (), (description, weight, public_key))
BOOST_DESCRIBE_STRUCT(finalizer_policy, (), (generation, threshold, finalizers))
BOOST_DESCRIBE_STRUCT(block_ref, (),
                      (num, id, slot, finality_digest, active_policy_generation, pending_policy_generation))
BOOST_DESCRIBE_STRUCT(block_ref_digest_data, (), (num, slot, finality_digest, parent_slot))

} // namespace forge::chain::savanna
