module;

#include <array>
#include <cstdint>
#include <optional>

module forge.tests.aligned_multi_index;

import forge.chain.protocol.values;
import forge.contract.multi_index;

namespace forge::tests::aligned_multi_index {

using forge::chain::protocol::literals::operator""_n;

struct alignas(32) [[forge::table("alignedrows")]] record {
   std::uint64_t id = 0;
   std::uint64_t marker = 0;
   std::array<std::uint8_t, 640> payload{};

   [[nodiscard]] std::uint64_t primary_key() const {
      return id;
   }
};

using records =
   forge::contract::multi_index<"alignedrows"_n, record>;

void create(forge::contract::context& context) {
   auto rows = records{
      context.get_self(),
      context.get_self().value};
   rows.emplace(context.get_self(), [](auto& value) {
      value.id = 1;
      value.marker = 7;
      value.payload.front() = 11;
      value.payload.back() = 29;
   });
}

} // namespace forge::tests::aligned_multi_index
