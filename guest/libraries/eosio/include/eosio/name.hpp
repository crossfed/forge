#pragma once

#include <cstddef>
#include <string_view>

import forge.contract.compatibility_name;

namespace eosio {

struct name : forge::contract::compatibility::name {
   using forge::contract::compatibility::name::name;
};

namespace literals {

consteval name operator""_n(const char* value, std::size_t size) {
   return name{std::string_view{value, size}};
}

} // namespace literals

} // namespace eosio

using eosio::literals::operator""_n;
