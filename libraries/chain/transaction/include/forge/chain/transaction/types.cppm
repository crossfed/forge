module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module forge.chain.transaction.types;

export import forge.chain.protocol.transaction;
export import forge.crypto.signer.types;

import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.described;
import forge.variant.value;

export namespace forge::chain::transaction {

using compression_type = decltype(std::declval<chain::protocol::packed_transaction>().compression);

struct context {
   chain::protocol::chain_id chain;
   chain::protocol::block_id reference_block;
   chain::protocol::time_point_sec reference_time;

   bool operator==(const context&) const = default;
};

struct options {
   std::uint32_t expiration_seconds = 30;
   std::optional<chain::protocol::time_point_sec> expiration;
   std::uint32_t max_net_usage_bytes = 0;
   std::uint8_t max_cpu_usage_ms = 0;
   std::uint32_t delay_seconds = 0;
   compression_type compression = compression_type::none;

   bool operator==(const options&) const = default;
};

struct unsigned_transaction {
   chain::protocol::chain_id chain;
   chain::protocol::transaction value;
   std::vector<chain::protocol::bytes> context_free_data;
   compression_type compression = compression_type::none;
};

struct signing_key {
   crypto::signer::key_id id;
   chain::protocol::public_key public_key;

   bool operator==(const signing_key&) const = default;
};

struct prepared_transaction {
   chain::protocol::packed_transaction packed;

   bool operator==(const prepared_transaction&) const = default;
};

BOOST_DESCRIBE_STRUCT(unsigned_transaction, (), (chain, value, context_free_data, compression))
BOOST_DESCRIBE_STRUCT(prepared_transaction, (), (packed))

} // namespace forge::chain::transaction

FORGE_DECLARE_SERIALIZATION(forge::chain::transaction::unsigned_transaction)
FORGE_DECLARE_SERIALIZATION(forge::chain::transaction::prepared_transaction)
