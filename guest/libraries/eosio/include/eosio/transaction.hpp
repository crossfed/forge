#pragma once

#include <eosio/action.hpp>
#include <eosio/internal/transaction.hpp>
#include <eosio/serialize.hpp>

#include <utility>

import forge.contract.transaction;

namespace eosio {

using forge::chain::protocol::transaction_header;
using forge::contract::expiration;
using forge::contract::get_action;
using forge::contract::get_context_free_data;
using forge::contract::read_transaction;
using forge::contract::tapos_block_num;
using forge::contract::tapos_block_prefix;
using forge::contract::transaction_size;

class transaction : public forge::chain::protocol::transaction {
 public:
   transaction(time_point_sec expiration = time_point_sec{}) {
      this->expiration = expiration;
   }

   explicit transaction(forge::chain::protocol::transaction value)
       : forge::chain::protocol::transaction(std::move(value)) {}

   void send(const uint128_t& sender_id, name payer, bool replace_existing = false) const {
      const auto serialized = eosio::pack(*this);
      internal_use_do_not_use::send_deferred(&sender_id, payer.value, serialized.data(), serialized.size(),
                                             replace_existing);
   }
};

template <typename Stream> void raw_pack(Stream& stream, const transaction& value) {
   forge::chain::protocol::raw_pack(stream, static_cast<const forge::chain::protocol::transaction&>(value));
}

template <typename Stream> void raw_unpack(Stream& stream, transaction& value) {
   forge::chain::protocol::raw_unpack(stream, static_cast<forge::chain::protocol::transaction&>(value));
}

[[nodiscard]] inline transaction get_transaction() {
   return transaction{forge::contract::get_transaction()};
}

} // namespace eosio
