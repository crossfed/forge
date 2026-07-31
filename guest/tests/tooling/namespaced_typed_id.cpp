#include <cstdint>

import forge.chain.protocol.typed_id;
import forge.contract;

namespace application {

using id = forge::chain::protocol::typed_id<1, 2>;

} // namespace application

namespace workspace {

using id = forge::chain::protocol::typed_id<1, 1>;

} // namespace workspace

namespace repair {

using id = forge::chain::protocol::typed_id<6, 1>;

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
      storage::coding_scheme coding) {
      static_cast<void>(application_id);
      static_cast<void>(workspace_id);
      static_cast<void>(repair_id);
      static_cast<void>(coding);
   }
};
