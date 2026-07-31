export module product.contract.revision;

import forge.contract;
import product.chain.protocol;

export namespace product::contract::revision {

void submit(
   forge::contract::context& context,
   const chain::begin_revision& request
);

} // namespace product::contract::revision
