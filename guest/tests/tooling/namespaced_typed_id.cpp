#include <cstdint>

import forge.db.ids.typed_id;
import forge.contract;
import forge.chain.protocol.activated_protocol_feature;
import forge.chain.protocol.blockchain_parameters;
import forge.chain.protocol.chain_config;
import forge.chain.protocol.elastic_limit_parameters;
import forge.chain.protocol.entity_selector;
import forge.chain.protocol.float128;
import forge.chain.protocol.float64;
import forge.chain.protocol.native_ids;
import forge.chain.protocol.usage_accumulator;
import forge.chain.protocol.wasm_parameters;

namespace application {

using id = forge::db::ids::typed_id<1, 2>;

} // namespace application

namespace workspace {

using id = forge::db::ids::typed_id<1, 1>;

} // namespace workspace

namespace repair {

using id = forge::db::ids::typed_id<6, 1>;

} // namespace repair

namespace storage {

enum class coding_scheme : std::uint8_t {
   replicated = 0,
   reed_solomon = 1,
};

} // namespace storage

class [[forge::contract("typedids")]] typed_ids final : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void submit(
      application::id application_id,
      workspace::id workspace_id,
      repair::id repair_id,
      forge::chain::protocol::account_id native_account_id,
      storage::coding_scheme coding) {
      const auto selector = forge::chain::protocol::account_selector{.id = native_account_id};
      static_cast<void>(forge::chain::protocol::chain_config{});
      static_cast<void>(forge::chain::protocol::wasm_parameters{});
      static_cast<void>(forge::chain::protocol::activated_protocol_feature{});
      static_cast<void>(forge::chain::protocol::float64{});
      static_cast<void>(forge::chain::protocol::float128{});
      static_cast<void>(application_id);
      static_cast<void>(workspace_id);
      static_cast<void>(repair_id);
      static_cast<void>(selector);
      static_cast<void>(coding);
   }

   [[forge::action]] void statevalues(
      forge::chain::protocol::account_selector selector,
      forge::chain::protocol::blockchain_parameters parameters,
      forge::chain::protocol::chain_config config,
      forge::chain::protocol::wasm_parameters wasm,
      forge::chain::protocol::elastic_limit_parameters elastic,
      forge::chain::protocol::usage_accumulator usage,
      forge::chain::protocol::activated_protocol_feature feature,
      forge::chain::protocol::float64 float64_value,
      forge::chain::protocol::float128 float128_value) {
      static_cast<void>(selector);
      static_cast<void>(parameters);
      static_cast<void>(config);
      static_cast<void>(wasm);
      static_cast<void>(elastic);
      static_cast<void>(usage);
      static_cast<void>(feature);
      static_cast<void>(float64_value);
      static_cast<void>(float128_value);
   }
};
