module;

#include <boost/describe.hpp>

#include <cstdint>
#include <vector>

export module forge.chain.savanna.vote;

export import forge.chain.savanna.types;

export namespace forge::chain::savanna {

enum class vote_kind : std::uint8_t {
   strong,
   weak,
};

struct finalizer_vote {
   block_id block;
   forge::crypto::bls::public_key finalizer;
   vote_kind kind = vote_kind::strong;
   forge::crypto::bls::signature signature;
};

enum class vote_result : std::uint8_t {
   accepted,
   duplicate,
   conflicting,
   wrong_block,
   unknown_finalizer,
   invalid_signature,
};

[[nodiscard]] std::vector<std::uint8_t>
message_for_vote(digest finality_digest, vote_kind kind);

BOOST_DESCRIBE_ENUM(vote_kind, strong, weak)
BOOST_DESCRIBE_STRUCT(finalizer_vote, (), (block, finalizer, kind, signature))
BOOST_DESCRIBE_ENUM(vote_result, accepted, duplicate, conflicting, wrong_block,
                    unknown_finalizer, invalid_signature)

} // namespace forge::chain::savanna
