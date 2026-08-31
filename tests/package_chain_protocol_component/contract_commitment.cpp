#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

import forge.chain.protocol.contract_commitment;
import forge.raw.raw;

bool contract_commitment_package_contract() {
   namespace protocol = forge::chain::protocol;
   const auto location = protocol::contract_table_location{.code = 1U, .scope = 2U, .table = 3U};
   const auto prefix = protocol::contract_index_prefix(location, protocol::contract_table_family::primary);
   const auto table_key = protocol::contract_table_key(location);
   const auto expected_prefix = std::vector<std::byte>{
       std::byte{0x01U}, std::byte{0x02U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
       std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U},
       std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
       std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
       std::byte{0x00U}, std::byte{0x03U}, std::byte{0x00U}, std::byte{0x1fU},
   };
   const auto secondary = std::array{std::byte{0x11U}, std::byte{0x22U}};
   const auto key =
       protocol::contract_secondary_key(location, protocol::contract_table_family::secondary_u64, secondary, 9U);
   const auto key_prefix = protocol::contract_index_prefix(location, protocol::contract_table_family::secondary_u64);
   const auto decoded = protocol::decode_contract_secondary_key(key, key_prefix, secondary.size());
   const auto packed = forge::raw::pack(protocol::primary_value{
       .payer = protocol::account_name{7U},
       .row = {0xaaU, 0xbbU},
   });
   const auto expected_packed = std::vector<std::uint8_t>{
       0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0xaaU, 0xbbU,
   };
   const auto unpacked = forge::raw::unpack_exact<protocol::primary_value>(packed);
   const auto table_wire = forge::raw::pack(protocol::table_value{
       .payer = protocol::account_name{7U},
       .count = 0x1122'3344U,
   });
   const auto expected_table_wire = std::vector<std::uint8_t>{
       0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x44U, 0x33U, 0x22U, 0x11U,
   };
   return prefix == expected_prefix &&
          table_key == protocol::contract_index_prefix(location, protocol::contract_table_family::table) && decoded &&
          decoded->secondary == protocol::commitment_bytes{secondary.begin(), secondary.end()} &&
          decoded->primary == 9U && unpacked.payer == protocol::account_name{7U} &&
          unpacked.row == std::vector<std::uint8_t>{0xaaU, 0xbbU} && packed == expected_packed &&
          table_wire == expected_table_wire;
}
