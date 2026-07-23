module;

#include <boost/describe.hpp>

#include <vector>

export module forge.chain.savanna.validation;

export import forge.chain.savanna.types;
export import forge.chain.savanna.exceptions;
export import forge.chain.core.merkle;

export namespace forge::chain::savanna {

struct validation_leaf {
   block_num_t num = 0;
   block_slot_t slot = 0;
   block_slot_t parent_slot = 0;
   digest finality_digest;
   digest commitment;
};

struct validation_state {
   forge::chain::core::incremental_merkle_tree tree;
   block_num_t first = 0;
   std::vector<digest> roots;
   std::vector<digest> leaves;
};

[[nodiscard]] validation_state make_validation(const validation_leaf& genesis);
[[nodiscard]] validation_state append(validation_state state, const validation_leaf& leaf);
[[nodiscard]] digest root_at(const validation_state& state, block_num_t num);
void validate(const validation_state& state);

BOOST_DESCRIBE_STRUCT(validation_leaf, (), (num, slot, parent_slot, finality_digest, commitment))
BOOST_DESCRIBE_STRUCT(validation_state, (), (tree, first, roots, leaves))

} // namespace forge::chain::savanna
