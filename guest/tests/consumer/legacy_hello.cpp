#include <cstdint>
#include <string>
#include <vector>

#include <eosio/base64.hpp>
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

   [[eosio::action]] std::string bafourlines() {
      const auto standard = eosio::base64_decode("YWJj\r\nZGVm");
      const auto url = eosio::base64url_decode("YWJj\r\nZGVm");
      eosio::check(standard == "abcdef", "legacy base64 decoder rejected CR/LF");
      eosio::check(url == "abcdef", "legacy base64url decoder rejected CR/LF");
      return standard;
   }
};

EOSIO_DISPATCH(legacyhello, (greet)(echo)(arraywire)(bafourlines))
