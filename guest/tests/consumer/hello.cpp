#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "local_value.hpp"

import forge.contract;

class [[forge::contract("hello")]] hello : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void greet(std::string user, std::vector<std::uint32_t> values) {
      forge::contract::check(!user.empty(), "user must not be empty");
      forge::contract::check(!values.empty(), "values must not be empty");
   }

   [[forge::action]] void values(forge::chain::protocol::name account, forge::chain::protocol::symbol symbol,
                                 forge::chain::protocol::asset quantity) {
      forge::contract::check(account.value == 6138663577826885632ULL, "unexpected name encoding");
      forge::contract::check(symbol.raw() == 1398362884ULL, "unexpected symbol encoding");
      forge::contract::check(quantity.amount == 42 && quantity.sym == symbol, "unexpected asset encoding");
   }

   [[forge::action]] long add(long left, long right) {
      return contract_fixture::add(left, right);
   }

   [[forge::action]] std::uint32_t answer() {
      return 42U;
   }

   [[forge::action]] std::uint32_t constanswer() const {
      return 43U;
   }

   [[forge::action]] std::uint32_t readsize(std::uint32_t value) const {
      static_cast<void>(value);
      return forge::contract::read_action_data(nullptr, 0U);
   }

   [[forge::action]] void containers(std::map<std::string, std::string> values, std::set<std::uint32_t> ordered,
                                     std::deque<std::uint32_t> queued, std::list<std::uint32_t> linked) {
      const auto found = values.find("answer");
      forge::contract::check(found != values.end() && found->second == "42", "unexpected map value");
      forge::contract::check(ordered.size() == 2 && ordered.contains(1) && ordered.contains(2), "unexpected set value");
      forge::contract::check(queued.size() == 2 && queued.front() == 3 && queued.back() == 4, "unexpected deque value");
      forge::contract::check(linked.size() == 2 && linked.front() == 5 && linked.back() == 6, "unexpected list value");
   }

   [[forge::on_notify("eosio.token::transfer")]] std::uint32_t transfer(std::uint32_t value) {
      return value + 1U;
   }

   [[forge::on_notify("*::fallback")]] std::uint32_t fallback(std::uint32_t value) {
      return value + 2U;
   }
};
