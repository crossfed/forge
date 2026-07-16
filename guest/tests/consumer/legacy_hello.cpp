#include <cstdint>
#include <string>
#include <vector>

#include <eosio/eosio.hpp>

class [[eosio::contract("legacyhello")]] legacyhello : public eosio::contract {
 public:
   using contract::contract;

   [[eosio::action]] void greet(std::string user, std::vector<std::uint32_t> values) {
      eosio::check(!user.empty(), "user must not be empty");
      eosio::check(!values.empty(), "values must not be empty");
   }
};

EOSIO_DISPATCH(legacyhello, (greet))
