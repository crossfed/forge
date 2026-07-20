module;

#include <cstddef>
#include <cstdint>
#include <vector>

export module forge.contract.deferred_transaction;

export import forge.chain.protocol.system;
export import forge.contract.transaction;

import forge.contract.datastream;
import forge.contract.system;

export namespace forge::contract {

class deferred_transaction : public chain::protocol::transaction {
 public:
   deferred_transaction(
       chain::protocol::time_point_sec expiration = chain::protocol::time_point_sec{current_time_point()} + 60U);

   void send(const chain::protocol::uint128_t& sender_id, chain::protocol::name payer,
             bool replace_existing = false) const;
};

struct onerror : chain::protocol::onerror {
   [[nodiscard]] static onerror from_current_action();
   [[nodiscard]] chain::protocol::transaction unpack_sent_trx() const;
};

template <typename Stream> void raw_pack(Stream& stream, const onerror& value) {
   chain::protocol::raw_pack(stream, static_cast<const chain::protocol::onerror&>(value));
}

template <typename Stream> void raw_unpack(Stream& stream, onerror& value) {
   chain::protocol::raw_unpack(stream, static_cast<chain::protocol::onerror&>(value));
}

void send_deferred(const chain::protocol::uint128_t& sender_id, chain::protocol::name payer, const char* transaction,
                   std::size_t size, bool replace_existing = false);
[[nodiscard]] bool cancel_deferred(const chain::protocol::uint128_t& sender_id);

} // namespace forge::contract
