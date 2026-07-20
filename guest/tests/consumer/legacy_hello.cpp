#include <cstdint>
#include <string>
#include <vector>

#include <eosio/eosio.hpp>

struct legacy_record {
   std::string user;
   std::vector<std::uint32_t> values;

   EOSLIB_SERIALIZE(legacy_record, (user)(values))
};

class [[eosio::contract("legacyhello")]] legacyhello : public eosio::contract {
 public:
   using contract::contract;

   [[eosio::action]] void greet(std::string user, std::vector<std::uint32_t> values) {
      eosio::check(!user.empty(), "user must not be empty");
      eosio::check(!values.empty(), "values must not be empty");
   }

   [[eosio::action]] legacy_record echo(legacy_record value) {
      return value;
   }

   [[eosio::action]] std::vector<char> arraywire() {
      const std::uint32_t values[2]{7U, 9U};
      auto result = std::vector<char>(9);
      auto stream = eosio::datastream<char*>{result.data(), result.size()};
      stream << values;
      return result;
   }
};

EOSIO_DISPATCH(legacyhello, (greet)(echo)(arraywire))
