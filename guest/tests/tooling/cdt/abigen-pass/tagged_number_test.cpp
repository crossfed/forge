#include <cstdint>

import forge.contract;

#include "../cdt_support.hpp"

using namespace eosio;

template <std::uint64_t Tag> struct TaggedNumber {
   std::uint64_t value;
};

class [[eosio::contract]] tagged_number_test : public contract {
 public:
   using contract::contract;

   [[eosio::action]] void test(TaggedNumber<"a.tag"_n.value>) {}
};
