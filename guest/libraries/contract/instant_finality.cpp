module;

#include <forge/contract/internal/intrinsics.hpp>

#include <vector>

module forge.contract.instant_finality;

import forge.contract.intrinsics;
import forge.raw.codec;

namespace forge::contract {

void set_finalizers(const finalizer_policy& policy) {
   const auto bytes = forge::raw::pack(policy);
   internal::set_finalizers(0U, reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace forge::contract
