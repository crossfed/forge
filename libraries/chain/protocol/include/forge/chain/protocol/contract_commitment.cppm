module;

#include <boost/describe.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module forge.chain.protocol.contract_commitment;

export import forge.chain.protocol.values;

export namespace forge::chain::protocol {

using commitment_bytes = std::vector<std::byte>;

inline constexpr std::uint8_t contract_commitment_key_version = 1U;
inline constexpr std::size_t contract_commitment_prefix_size = 28U;

enum class contract_table_family : std::uint16_t {
   table = 30U,
   primary = 31U,
   secondary_u64 = 32U,
   secondary_u128 = 33U,
   secondary_u256 = 34U,
   secondary_f64 = 35U,
   secondary_f128 = 36U,
};

struct contract_table_location {
   std::uint64_t code = 0;
   std::uint64_t scope = 0;
   std::uint64_t table = 0;

   bool operator==(const contract_table_location&) const = default;
};

struct table_value {
   account_name payer;
   std::uint32_t count = 0;

   bool operator==(const table_value&) const = default;
};

struct primary_value {
   account_name payer;
   std::vector<std::uint8_t> row;

   bool operator==(const primary_value&) const = default;
};

struct secondary_value {
   account_name payer;
   std::uint64_t primary = 0;

   bool operator==(const secondary_value&) const = default;
};

struct secondary_key_position {
   commitment_bytes secondary;
   std::uint64_t primary = 0;

   bool operator==(const secondary_key_position&) const = default;
};

[[nodiscard]] commitment_bytes contract_table_key(contract_table_location location);
[[nodiscard]] commitment_bytes contract_index_prefix(contract_table_location location, contract_table_family family);
[[nodiscard]] commitment_bytes contract_primary_key(contract_table_location location, contract_table_family family,
                                                    std::uint64_t primary);
[[nodiscard]] commitment_bytes contract_secondary_prefix(contract_table_location location, contract_table_family family,
                                                         std::span<const std::byte> secondary);
[[nodiscard]] commitment_bytes contract_secondary_key(contract_table_location location, contract_table_family family,
                                                      std::span<const std::byte> secondary, std::uint64_t primary);
[[nodiscard]] std::optional<secondary_key_position> decode_contract_secondary_key(std::span<const std::byte> key,
                                                                                  std::span<const std::byte> prefix,
                                                                                  std::size_t secondary_size);

BOOST_DESCRIBE_STRUCT(contract_table_location, (), (code, scope, table))
BOOST_DESCRIBE_STRUCT(table_value, (), (payer, count))
BOOST_DESCRIBE_STRUCT(primary_value, (), (payer, row))
BOOST_DESCRIBE_STRUCT(secondary_value, (), (payer, primary))

} // namespace forge::chain::protocol
