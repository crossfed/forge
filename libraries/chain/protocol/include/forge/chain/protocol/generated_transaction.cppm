module;

#include <boost/describe.hpp>

export module forge.chain.protocol.generated_transaction;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.time;
export import forge.chain.protocol.transaction;
export import forge.chain.protocol.types;

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

} // namespace forge::chain::protocol

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(generated_transaction, (),
                      (id, trx_id, sender, sender_id, payer, delay_until, expiration, published, transaction))
} // namespace forge::chain::protocol
