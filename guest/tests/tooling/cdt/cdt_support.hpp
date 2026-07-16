#pragma once

#include <cstddef>
#include <cstdint>

namespace eosio {

using contract = forge::contract::base;
using forge::chain::protocol::asset;
using forge::chain::protocol::name;

struct literal_name {
   std::uint64_t value = 0;

   consteval operator std::uint64_t() const {
      return value;
   }

   consteval operator name() const {
      return name{value};
   }
};

} // namespace eosio

consteval eosio::literal_name operator""_n(const char* text, std::size_t size) {
   constexpr auto alphabet = ".12345abcdefghijklmnopqrstuvwxyz";
   auto result = std::uint64_t{0};
   for (auto index = std::size_t{0}; index < 13U; ++index) {
      auto encoded = std::uint8_t{0};
      if (index < size) {
         for (auto position = std::uint8_t{0}; position < 32U; ++position) {
            if (alphabet[position] == text[index]) {
               encoded = position;
               break;
            }
         }
      }
      if (index < 12U) {
         result |= (static_cast<std::uint64_t>(encoded) & 0x1fULL) << (64U - 5U * (index + 1U));
      } else {
         result |= static_cast<std::uint64_t>(encoded) & 0x0fULL;
      }
   }
   return eosio::literal_name{result};
}

namespace eosio {

template <std::uint64_t Name, typename Value> struct multi_index {
   using value_type = Value;
};

template <std::uint64_t Name, typename Value> struct singleton {
   using value_type = Value;
};

inline void require_auth(name) {}

template <typename... Values> inline void print(Values&&...) {}

template <typename... Values> inline void print_f(const char*, Values&&...) {}

} // namespace eosio
