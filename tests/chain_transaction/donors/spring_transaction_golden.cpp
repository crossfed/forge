#include <eosio/chain/transaction.hpp>
#include <fc/io/raw.hpp>

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <utility>

int main() {
   auto value = eosio::chain::transaction{};
   value.expiration = eosio::chain::time_point_sec{1U};
   value.ref_block_num = 1U;
   value.ref_block_prefix = 0xaabbccddU;

   auto action = eosio::chain::action{};
   action.account = eosio::chain::account_name{std::uint64_t{0x5530ea0000000000U}}; // eosio
   action.name = eosio::chain::action_name{std::uint64_t{0xc2b263b800000000U}};     // setabi
   action.data = {0x01, 0x02};
   value.actions.push_back(std::move(action));

   const auto packed = fc::raw::pack(value);
   std::cout << std::hex << std::setfill('0');
   for (const auto byte : packed) {
      std::cout << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(byte));
   }
   std::cout << '\n';
}
