#pragma once

#include <eosio/system.hpp>
#include <eosio/transaction.hpp>

#include <vector>

import forge.contract.deferred_transaction;

namespace eosio {

using forge::contract::cancel_deferred;
using forge::contract::send_deferred;

class deferred_transaction : public transaction {
 public:
   deferred_transaction(time_point_sec expiration = time_point_sec{forge::contract::current_time_point()} + 60U)
       : transaction(expiration) {}

   void send(const uint128_t& sender_id, name payer, bool replace_existing = false) const {
      const auto serialized = eosio::pack(static_cast<const transaction&>(*this));
      forge::contract::send_deferred(sender_id, payer, serialized.data(), serialized.size(), replace_existing);
   }
};

struct onerror {
   uint128_t sender_id = 0;
   std::vector<char> sent_trx;

   [[nodiscard]] static onerror from_current_action() {
      return forge::contract::unpack_action_data<onerror>();
   }

   [[nodiscard]] transaction unpack_sent_trx() const {
      return eosio::unpack<transaction>(sent_trx);
   }

   EOSLIB_SERIALIZE(onerror, (sender_id)(sent_trx))
};

} // namespace eosio
