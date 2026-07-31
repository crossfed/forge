#include <concepts>
#include <optional>

import forge.chain.protocol.block;
import forge.chain.protocol.finalizer_policy;
import forge.chain.protocol.producer_authority;
import forge.chain.protocol.producer_schedule;
import forge.variant.described;
import forge.variant.static_variant;
import forge.variant.value;

static_assert(std::same_as<decltype(forge::chain::protocol::block_header::new_producers),
                           std::optional<forge::chain::protocol::producer_schedule>>);

void use_installed_producer_schedule() {
   auto schedule = forge::chain::protocol::producer_schedule{};
   auto authority_schedule = forge::chain::protocol::producer_authority_schedule{};
   auto finalizer_policy = forge::chain::protocol::finalizer_policy{};
   auto encoded = forge::variant{authority_schedule};
   auto decoded = forge::chain::protocol::producer_authority_schedule{};
   forge::from_variant(encoded, decoded);
   auto finalizer_encoded = forge::variant{finalizer_policy};
   auto finalizer_decoded = forge::chain::protocol::finalizer_policy{};
   forge::from_variant(finalizer_encoded, finalizer_decoded);
   (void)schedule;
   (void)authority_schedule;
   (void)finalizer_policy;
   (void)decoded;
   (void)finalizer_decoded;
}
