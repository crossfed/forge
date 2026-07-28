#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

import forge.chain.protocol.values;
import forge.contract.testing.host;
import forge.raw.codec;
import product.chain.protocol;

namespace {

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
   auto input = std::ifstream{path, std::ios::binary | std::ios::ate};
   if (!input) {
      throw std::runtime_error{"cannot open dual-target contract"};
   }
   const auto size = input.tellg();
   if (size < 0) {
      throw std::runtime_error{"cannot determine dual-target contract size"};
   }
   auto result = std::vector<std::uint8_t>(static_cast<std::size_t>(size));
   input.seekg(0);
   if (!result.empty() && !input.read(reinterpret_cast<char*>(result.data()), size)) {
      throw std::runtime_error{"cannot read dual-target contract"};
   }
   return result;
}

} // namespace

int main() {
   namespace protocol = forge::chain::protocol;

   const auto code = read_bytes(PRODUCT_PROTOCOL_WASM);
   const auto request = product::chain::begin_revision{
       .workspace = product::chain::workspace_id{7},
       .inode = product::chain::inode_id{11},
       .size = 4096,
   };
   const auto account = protocol::make_name("product").value;
   auto host = forge::contract::testing::host{};
   constexpr auto begin_revision_name = product::chain::begin_revision::get_name();
   host.invoke(code, account, account, begin_revision_name.value, forge::raw::pack(request));

   const auto row = host.find_primary(account, request.workspace.value, protocol::make_name("revisions").value, 0);
   assert(row.has_value());
   const auto expected = product::chain::revision{
       .id = 0,
       .workspace = request.workspace,
       .inode = request.inode,
       .size = request.size,
       .state = product::chain::revision_state::preparing,
   };
   assert(row->value == forge::raw::pack(expected));
}
