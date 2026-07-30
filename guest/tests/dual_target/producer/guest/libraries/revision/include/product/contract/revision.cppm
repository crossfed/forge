export module product.contract.revision;

import forge.contract;
import product.chain.protocol;

export namespace product::contract::revision {

void submit(forge::contract::context& context, chain::begin_revision const& request);

}
