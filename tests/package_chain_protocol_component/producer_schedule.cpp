#include <concepts>
#include <optional>

import forge.chain.protocol.block;
import forge.chain.protocol.producer_authority;
import forge.chain.protocol.producer_schedule;

static_assert(std::same_as<decltype(forge::chain::protocol::block_header::new_producers),
                           std::optional<forge::chain::protocol::producer_schedule>>);

void use_installed_producer_schedule() {
   auto schedule = forge::chain::protocol::producer_schedule{};
   auto authority_schedule = forge::chain::protocol::producer_authority_schedule{};
   (void)schedule;
   (void)authority_schedule;
}
