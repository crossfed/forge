#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

import forge.chain.protocol.values;
import forge.contract.testing.host;

namespace {

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
   auto input = std::ifstream{path, std::ios::binary | std::ios::ate};
   if (!input) {
      throw std::runtime_error{"cannot open aligned multi-index contract"};
   }
   const auto size = input.tellg();
   if (size < 0) {
      throw std::runtime_error{"cannot determine aligned multi-index contract size"};
   }
   auto result = std::vector<std::uint8_t>(static_cast<std::size_t>(size));
   input.seekg(0);
   if (!result.empty() && !input.read(reinterpret_cast<char*>(result.data()), size)) {
      throw std::runtime_error{"cannot read aligned multi-index contract"};
   }
   return result;
}

} // namespace

int main() {
   namespace protocol = forge::chain::protocol;

   const auto code = read_bytes(ALIGNED_MULTI_INDEX_WASM);
   const auto account = protocol::make_name("alignedidx").value;
   auto host = forge::contract::testing::host{};
   host.invoke(code, account, account, protocol::make_name("create").value);

   const auto row =
      host.find_primary(account, account, protocol::make_name("alignedrows").value, 1);
   assert(row.has_value());
   assert(row->value.size() == 656);
   assert(row->value[8] == 7);
   assert(row->value[16] == 11);
   assert(row->value.back() == 29);
}
