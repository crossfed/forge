module;

#include <boost/describe.hpp>

export module forge.chain.savanna.candidate;

export import forge.chain.savanna.header_state;
export import forge.chain.savanna.rank;
export import forge.chain.savanna.validation;

export namespace forge::chain::savanna {

struct candidate {
   block_id id;
   block_id previous;
   block_num num = 0;
   block_timestamp timestamp;
   forge::chain::savanna::rank fork_rank;
   header_state state;
   forge::chain::savanna::validation_state validation;
   digest action_receipt_root;
};

BOOST_DESCRIBE_STRUCT(candidate, (), (id, previous, num, timestamp, fork_rank, state, validation, action_receipt_root))

} // namespace forge::chain::savanna
