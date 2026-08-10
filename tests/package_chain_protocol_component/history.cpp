#include <cstdint>

import forge.chain.protocol.history_query;
import forge.raw.raw;
import forge.variant.described;
import forge.variant.value;

bool history_query_roundtrip() {
   namespace protocol = forge::chain::protocol;

   auto id = protocol::transaction_id{};
   id._hash[0] = 0x42U;
   const auto request = protocol::transaction_history_request{
       .id = id,
       .audit = protocol::audit_mode::required,
   };
   const auto canonical = forge::raw::pack(request);
   const auto raw_decoded = forge::raw::unpack_exact<protocol::transaction_history_request>(canonical);

   auto encoded = forge::variant{};
   forge::to_variant(request, encoded);
   auto variant_decoded = protocol::transaction_history_request{};
   forge::from_variant(encoded, variant_decoded);
   return raw_decoded == request && variant_decoded == request;
}
