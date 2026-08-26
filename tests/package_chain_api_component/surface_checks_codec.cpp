module;

#include <cstdint>

module package.chain_api_component.surface_checks;

import package.chain_api_component.test_support;
import forge.chain.api.table_key;
import forge.chain.protocol.block_query;
import forge.chain.protocol.state_query;
import forge.chain.protocol.transaction_query;

namespace package_chain_api_component {

void check_codec_surface() {
   namespace chain_api = forge::chain::api;
   namespace protocol = forge::chain::protocol;

   auto request = protocol::table_changes_request{
       .from_block = 39,
       .to_block = 40,
       .tables = {{.code = protocol::account_name{"tester"}}},
   };
   auto block = protocol::block_request{};
   auto transaction = protocol::transaction_status_request{};
   const auto table_key = chain_api::encode_table_key(std::uint64_t{42U});
   static_cast<void>(request);
   static_cast<void>(block);
   static_cast<void>(transaction);
   require(table_key.size() == sizeof(std::uint64_t), "installed table key codec returned the wrong width");
}

} // namespace package_chain_api_component
