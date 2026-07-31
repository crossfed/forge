module;

#include <cstdint>

export module product.contract.configuration;

import forge.chain.protocol.values;
import forge.contract;

export namespace product::contract::configuration {

struct request {
   std::uint32_t value{0};

   static constexpr forge::chain::protocol::action_name get_name() {
      return forge::chain::protocol::make_name("verify");
   }
};

void verify(forge::contract::context& context, const request& value);

} // namespace product::contract::configuration
