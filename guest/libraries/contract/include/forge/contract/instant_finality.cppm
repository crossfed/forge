module;

#include <cstdint>
#include <string>
#include <vector>

export module forge.contract.instant_finality;

export import forge.chain.protocol.finalizer_policy;
export import forge.contract.crypto_bls_ext;

import forge.contract.datastream;
import forge.contract.intrinsics;

export namespace forge::contract {

using chain::protocol::finalizer_authority;
using chain::protocol::finalizer_policy;

void set_finalizers(const finalizer_policy& policy);

} // namespace forge::contract
