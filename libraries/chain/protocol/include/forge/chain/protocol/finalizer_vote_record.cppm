module;

#include <boost/describe.hpp>

#include <cstdint>
#include <string>

export module forge.chain.protocol.finalizer_vote_record;

export import forge.chain.protocol.types;
export import forge.crypto.bls.serialization;

export namespace forge::chain::protocol {

struct finalizer_vote_record {
   std::string description;
   forge::crypto::bls::public_key public_key;
   bool is_vote_strong = false;
   std::uint32_t finalizer_policy_generation = 0;
   block_id voted_for_block_id{};
   std::uint32_t voted_for_block_num = 0;
   block_timestamp voted_for_block_timestamp{};

   bool operator==(const finalizer_vote_record&) const = default;
};

BOOST_DESCRIBE_STRUCT(finalizer_vote_record, (),
                      (description, public_key, is_vote_strong, finalizer_policy_generation, voted_for_block_id,
                       voted_for_block_num, voted_for_block_timestamp))

} // namespace forge::chain::protocol
