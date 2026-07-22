#pragma once

#include <eosio/crypto.hpp>
import forge.contract.crypto_ext;

namespace eosio {

using forge::contract::alt_bn128_add;
using forge::contract::alt_bn128_mul;
using forge::contract::alt_bn128_pair;
using forge::contract::bigint;
using forge::contract::blake2_f;
using forge::contract::blake2f_result_size;
using forge::contract::ec_point;
using forge::contract::ec_point_view;
using forge::contract::g1_coordinate_size;
using forge::contract::g1_point;
using forge::contract::g1_point_view;
using forge::contract::g2_coordinate_size;
using forge::contract::g2_point;
using forge::contract::g2_point_view;
using forge::contract::k1_recover;
using forge::contract::mod_exp;

[[nodiscard]] inline checksum256 sha3(const char* data, std::uint32_t size) {
   return detail::from_digest<32>(forge::contract::sha3(data, size));
}

inline void assert_sha3(const char* data, std::uint32_t size, const checksum256& expected) {
   forge::contract::assert_sha3(data, size, detail::to_digest<forge::contract::checksum256>(expected));
}

[[nodiscard]] inline checksum256 keccak(const char* data, std::uint32_t size) {
   return detail::from_digest<32>(forge::contract::keccak(data, size));
}

inline void assert_keccak(const char* data, std::uint32_t size, const checksum256& expected) {
   forge::contract::assert_keccak(data, size, detail::to_digest<forge::contract::checksum256>(expected));
}

} // namespace eosio
