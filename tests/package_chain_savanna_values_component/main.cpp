#include <cstdint>

import forge.chain.savanna.values;
import forge.raw.raw;
import forge.variant.described;
import forge.variant.value;

int main() {
   const auto key = forge::crypto::bls::encoding::parse_public_key(
       "PUB_BLS_82P3oM1u0IEv64u9i4vSzvg1-"
       "QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg");
   const auto policy = forge::chain::savanna::finalizer_policy{
       .generation = 1U,
       .threshold = 1U,
       .finalizers = {{.description = "package", .weight = 1U, .public_key = key}},
   };

   const auto encoded = forge::variant{policy};
   auto decoded = forge::chain::savanna::finalizer_policy{};
   forge::from_variant(encoded, decoded);
   const auto raw = forge::raw::pack(policy);
   return decoded == policy && !raw.empty() ? 0 : 1;
}
