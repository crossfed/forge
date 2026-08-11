#pragma once

#include <eosio/check.hpp>
#include <eosio/serialize.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

import forge.contract.instant_finality;

namespace eosio {

struct finalizer_authority {
   std::string description;
   std::uint64_t weight = 0;
   std::vector<char> public_key;

   EOSLIB_SERIALIZE(finalizer_authority, (description)(weight)(public_key))
};

struct finalizer_policy {
   std::uint64_t threshold = 0;
   std::vector<finalizer_authority> finalizers;

   EOSLIB_SERIALIZE(finalizer_policy, (threshold)(finalizers))
};

[[nodiscard]] inline forge::chain::savanna::finalizer to_protocol(const finalizer_authority& value) {
   check(value.public_key.size() == forge::crypto::bls::public_key::size_bytes, "public key has a wrong size");
   auto bytes = forge::crypto::bls::public_key::data_type{};
   for (auto index = std::size_t{}; index < bytes.size(); ++index) {
      bytes[index] = static_cast<std::uint8_t>(value.public_key[index]);
   }
   return {
       .description = value.description,
       .weight = value.weight,
       .public_key = forge::crypto::bls::public_key{bytes},
   };
}

[[nodiscard]] inline forge::chain::protocol::finalizer_policy to_protocol(const finalizer_policy& value) {
   auto result = forge::chain::protocol::finalizer_policy{.threshold = value.threshold};
   result.finalizers.reserve(value.finalizers.size());
   for (const auto& finalizer : value.finalizers) {
      result.finalizers.push_back(to_protocol(finalizer));
   }
   return result;
}

inline void set_finalizers(const finalizer_policy& value) {
   forge::contract::set_finalizers(to_protocol(value));
}

} // namespace eosio
