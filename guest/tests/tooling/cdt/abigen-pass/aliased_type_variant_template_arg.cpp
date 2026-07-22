#include <cstdint>
#include <string>
#include <variant>

import forge.contract;

#include "../cdt_support.hpp"

using namespace eosio;

using str = std::string;

class [[eosio::contract]] aliased_type_variant_template_arg : public contract {
 public:
   using contract::contract;

   [[eosio::action]] void hi(std::variant<std::uint64_t, str> v) {
      if (std::holds_alternative<std::uint64_t>(v)) {
      } else if (std::holds_alternative<str>(v)) {
      }
   }
};
