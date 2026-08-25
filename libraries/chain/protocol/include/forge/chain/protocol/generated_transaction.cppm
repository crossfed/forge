module;

#include <boost/describe.hpp>

export module forge.chain.protocol.generated_transaction;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.time;
export import forge.chain.protocol.transaction;
export import forge.chain.protocol.types;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct generated_transaction {
   generated_transaction_id id;
   transaction_id trx_id;
   account_name sender;
   uint128_t sender_id = 0;
   account_name payer;
   time_point delay_until{};
   time_point expiration{};
   time_point published{};
   packed_transaction transaction;

   bool operator==(const generated_transaction&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const generated_transaction& value) {
   forge::raw::pack(stream, value.id);
   forge::raw::pack(stream, value.trx_id);
   forge::raw::pack(stream, value.sender);
   forge::raw::pack(stream, value.sender_id);
   forge::raw::pack(stream, value.payer);
   forge::raw::pack(stream, value.delay_until);
   forge::raw::pack(stream, value.expiration);
   forge::raw::pack(stream, value.published);
   forge::raw::pack(stream, value.transaction);
}

template <typename Stream> void raw_unpack(Stream& stream, generated_transaction& value) {
   forge::raw::unpack(stream, value.id);
   forge::raw::unpack(stream, value.trx_id);
   forge::raw::unpack(stream, value.sender);
   forge::raw::unpack(stream, value.sender_id);
   forge::raw::unpack(stream, value.payer);
   forge::raw::unpack(stream, value.delay_until);
   forge::raw::unpack(stream, value.expiration);
   forge::raw::unpack(stream, value.published);
   forge::raw::unpack(stream, value.transaction);
}

} // namespace forge::chain::protocol

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(generated_transaction, (),
                      (id, trx_id, sender, sender_id, payer, delay_until, expiration, published, transaction))
} // namespace forge::chain::protocol
