module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstddef>

module forge.contract.deferred_transaction;

import forge.contract.system;
import forge.raw.codec;

namespace forge::contract {

deferred_transaction::deferred_transaction(chain::protocol::time_point_sec expiration) {
   this->expiration = expiration;
}

void deferred_transaction::send(const chain::protocol::uint128_t& sender_id, chain::protocol::name payer,
                                bool replace_existing) const {
   const auto serialized = forge::raw::pack(static_cast<const chain::protocol::transaction&>(*this));
   internal::send_deferred(&sender_id, payer.value, reinterpret_cast<const char*>(serialized.data()), serialized.size(),
                           replace_existing ? 1U : 0U);
}

onerror onerror::from_current_action() {
   return unpack_action_data<onerror>();
}

chain::protocol::transaction onerror::unpack_sent_trx() const {
   return forge::raw::unpack_exact<chain::protocol::transaction>(sent_trx);
}

void send_deferred(const chain::protocol::uint128_t& sender_id, chain::protocol::name payer, const char* transaction,
                   std::size_t size, bool replace_existing) {
   internal::send_deferred(&sender_id, payer.value, transaction, size, replace_existing ? 1U : 0U);
}

bool cancel_deferred(const chain::protocol::uint128_t& sender_id) {
   return internal::cancel_deferred(&sender_id) != 0;
}

} // namespace forge::contract
