module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

export module forge.contract.deferred_transaction;

export import forge.contract.transaction;

import forge.contract.datastream;
import forge.contract.system;

export namespace forge::contract {

class deferred_transaction : public chain::protocol::transaction {
 public:
   deferred_transaction(
       chain::protocol::time_point_sec expiration = chain::protocol::time_point_sec{current_time_point()} + 60U) {
      this->expiration = expiration;
   }

   void send(const chain::protocol::uint128_t& sender_id, chain::protocol::name payer,
             bool replace_existing = false) const {
      const auto serialized = ::forge::raw::pack(static_cast<const chain::protocol::transaction&>(*this));
      ::forge::contract::internal::send_deferred(&sender_id, payer.value,
                                                 reinterpret_cast<const char*>(serialized.data()), serialized.size(),
                                                 replace_existing ? 1U : 0U);
   }
};

struct onerror {
   chain::protocol::uint128_t sender_id = 0;
   std::vector<std::uint8_t> sent_trx;

   [[nodiscard]] static onerror from_current_action() {
      return unpack_action_data<onerror>();
   }

   [[nodiscard]] chain::protocol::transaction unpack_sent_trx() const {
      return ::forge::raw::unpack_exact<chain::protocol::transaction>(sent_trx);
   }
};

template <typename Stream> void raw_pack(Stream& stream, const onerror& value) {
   ::forge::raw::pack(stream, value.sender_id);
   ::forge::raw::pack(stream, value.sent_trx);
}

template <typename Stream> void raw_unpack(Stream& stream, onerror& value) {
   ::forge::raw::unpack(stream, value.sender_id);
   ::forge::raw::unpack(stream, value.sent_trx);
}

inline void send_deferred(const chain::protocol::uint128_t& sender_id, chain::protocol::name payer,
                          const char* transaction, std::size_t size, bool replace_existing = false) {
   ::forge::contract::internal::send_deferred(&sender_id, payer.value, transaction, size, replace_existing ? 1U : 0U);
}

[[nodiscard]] inline bool cancel_deferred(const chain::protocol::uint128_t& sender_id) {
   return ::forge::contract::internal::cancel_deferred(&sender_id) != 0;
}

} // namespace forge::contract
