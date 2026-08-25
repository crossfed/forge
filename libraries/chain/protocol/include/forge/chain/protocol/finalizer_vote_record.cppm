module;

#include <boost/describe.hpp>

#include <cstdint>
#include <string>

export module forge.chain.protocol.finalizer_vote_record;

export import forge.chain.protocol.types;
export import forge.crypto.bls.serialization;
import forge.raw.codec;

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

template <typename Stream> void raw_pack(Stream& stream, const finalizer_vote_record& value) {
   forge::raw::pack(stream, value.description);
   forge::raw::pack(stream, value.public_key);
   forge::raw::pack(stream, value.is_vote_strong);
   forge::raw::pack(stream, value.finalizer_policy_generation);
   forge::raw::pack(stream, value.voted_for_block_id);
   forge::raw::pack(stream, value.voted_for_block_num);
   forge::raw::pack(stream, value.voted_for_block_timestamp);
}

template <typename Stream> void raw_unpack(Stream& stream, finalizer_vote_record& value) {
   forge::raw::unpack(stream, value.description);
   forge::raw::unpack(stream, value.public_key);
   forge::raw::unpack(stream, value.is_vote_strong);
   forge::raw::unpack(stream, value.finalizer_policy_generation);
   forge::raw::unpack(stream, value.voted_for_block_id);
   forge::raw::unpack(stream, value.voted_for_block_num);
   forge::raw::unpack(stream, value.voted_for_block_timestamp);
}

BOOST_DESCRIBE_STRUCT(finalizer_vote_record, (),
                      (description, public_key, is_vote_strong, finalizer_policy_generation, voted_for_block_id,
                       voted_for_block_num, voted_for_block_timestamp))

} // namespace forge::chain::protocol
