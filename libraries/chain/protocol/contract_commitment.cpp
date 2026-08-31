module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

module forge.chain.protocol.contract_commitment;

namespace forge::chain::protocol {
namespace {

enum class contract_commitment_key_kind : std::uint8_t {
   contract = 2U,
};

template <typename Value> void append_ordered(commitment_bytes& output, Value value) {
   for (auto offset = sizeof(Value); offset > 0U; --offset) {
      output.push_back(static_cast<std::byte>((value >> ((offset - 1U) * 8U)) & 0xffU));
   }
}

std::uint64_t decode_ordered(std::span<const std::byte> value) {
   auto result = std::uint64_t{};
   for (const auto byte : value) {
      result = (result << 8U) | std::to_integer<std::uint8_t>(byte);
   }
   return result;
}

} // namespace

commitment_bytes contract_table_key(contract_table_location location) {
   return contract_index_prefix(location, contract_table_family::table);
}

commitment_bytes contract_index_prefix(contract_table_location location, contract_table_family family) {
   auto result = commitment_bytes{};
   result.reserve(contract_commitment_prefix_size);
   append_ordered(result, contract_commitment_key_version);
   append_ordered(result, static_cast<std::uint8_t>(contract_commitment_key_kind::contract));
   append_ordered(result, location.code);
   append_ordered(result, location.scope);
   append_ordered(result, location.table);
   append_ordered(result, static_cast<std::uint16_t>(family));
   return result;
}

commitment_bytes contract_primary_key(contract_table_location location, contract_table_family family,
                                      std::uint64_t primary) {
   auto result = contract_index_prefix(location, family);
   append_ordered(result, primary);
   return result;
}

commitment_bytes contract_secondary_prefix(contract_table_location location, contract_table_family family,
                                           std::span<const std::byte> secondary) {
   auto result = contract_index_prefix(location, family);
   result.insert(result.end(), secondary.begin(), secondary.end());
   return result;
}

commitment_bytes contract_secondary_key(contract_table_location location, contract_table_family family,
                                        std::span<const std::byte> secondary, std::uint64_t primary) {
   auto result = contract_secondary_prefix(location, family, secondary);
   append_ordered(result, primary);
   return result;
}

std::optional<secondary_key_position> decode_contract_secondary_key(std::span<const std::byte> key,
                                                                    std::span<const std::byte> prefix,
                                                                    std::size_t secondary_size) {
   constexpr auto primary_size = std::size_t{8U};
   if (key.size() != prefix.size() + secondary_size + primary_size ||
       !std::ranges::equal(prefix, key.first(prefix.size()))) {
      return std::nullopt;
   }

   return secondary_key_position{
       .secondary = commitment_bytes{key.begin() + static_cast<std::ptrdiff_t>(prefix.size()),
                                     key.end() - static_cast<std::ptrdiff_t>(primary_size)},
       .primary = decode_ordered(key.last(primary_size)),
   };
}

} // namespace forge::chain::protocol
