#include <cstdint>
#include <string>
#include <vector>

#include <eosio/eosio.hpp>

class [[eosio::contract("parity")]] parity : public eosio::contract {
 public:
   using contract::contract;

   [[eosio::action]] void greet(std::string user, std::vector<std::uint32_t> values) {}
};
