#pragma once

#include <eosio/internal/system.hpp>

#include <string_view>

import forge.contract.intrinsics;

namespace eosio {

using forge::contract::check;
using forge::contract::return_code;

[[deprecated("use check")]] inline void eosio_assert(bool condition, const char* message) {
   check(condition, message);
}

[[deprecated("use check")]] inline void eosio_assert_message(bool condition, const char* message, std::uint32_t size) {
   check(condition, std::string_view{message, size});
}

[[deprecated("use check")]] inline void eosio_assert_code(bool condition, std::uint64_t code) {
   if (!condition) {
      internal_use_do_not_use::eosio_assert_code(0U, code);
   }
}

} // namespace eosio
