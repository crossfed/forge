module;

#include <boost/describe.hpp>

#include <optional>

export module forge.chain.savanna.checkpoint;

export import forge.chain.savanna.header_state;
export import forge.chain.savanna.validation;

export namespace forge::chain::savanna {

struct checkpoint {
   forge::chain::savanna::block_ref finalized;
   header_state state;
   forge::chain::savanna::validation_state validation;
};

BOOST_DESCRIBE_STRUCT(checkpoint, (), (finalized, state, validation))

} // namespace forge::chain::savanna
