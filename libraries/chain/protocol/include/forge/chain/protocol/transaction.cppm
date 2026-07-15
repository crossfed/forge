module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <chrono>
#include <cstdint>
#include <new>
#include <string>
#include <utility>
#include <variant>
#include <vector>

export module forge.chain.protocol.transaction;

export import forge.raw.varint;

export import forge.chain.protocol.action;
import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.described;

export namespace forge::chain::protocol {

struct deferred_transaction_generation_context {
   static constexpr std::uint16_t extension_id() {
      return 0;
   }

   transaction_id sender_trx_id;
   uint128_t sender_id = 0;
   account_name sender;
};

using transaction_extension = std::variant<deferred_transaction_generation_context>;

struct transaction_header {
   std::chrono::sys_seconds expiration;
   std::uint16_t ref_block_num = 0;
   std::uint32_t ref_block_prefix = 0;
   forge::unsigned_int max_net_usage_words = 0;
   std::uint8_t max_cpu_usage_ms = 0;
   forge::unsigned_int delay_sec = 0;
};

struct transaction : transaction_header {
   std::vector<action> context_free_actions;
   std::vector<action> actions;
   extensions transaction_extensions;

   [[nodiscard]] transaction_id id() const;
   [[nodiscard]] core::digest sig_digest(const chain_id& chain_id, const std::vector<bytes>& cfd = {}) const;
};

struct signed_transaction : transaction {
   std::vector<signature> signatures;
   std::vector<bytes> context_free_data;
};

struct packed_transaction {
   enum class compression : std::uint8_t {
      none = 0,
      zlib = 1,
   };

   packed_transaction() = default;
   explicit packed_transaction(const signed_transaction& value, compression compression = compression::none);
   explicit packed_transaction(signed_transaction&& value, compression compression = compression::none);

   std::vector<signature> signatures;
   compression compression = compression::none;
   bytes packed_context_free_data;
   bytes packed_trx;

   [[nodiscard]] core::digest packed_digest() const;
   [[nodiscard]] transaction_id id() const;
   [[nodiscard]] signed_transaction get_signed_transaction() const;
};

bytes pack_transaction(const transaction& value);
transaction_id calculate_transaction_id(const transaction& value);
bytes signature_preimage(const chain_id& chain_id, const transaction& value, const std::vector<bytes>& cfd = {});
core::digest signature_digest(const chain_id& chain_id, const transaction& value, const std::vector<bytes>& cfd = {});

} // namespace forge::chain::protocol

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(deferred_transaction_generation_context, (), (sender_trx_id, sender_id, sender))
BOOST_DESCRIBE_STRUCT(transaction_header, (),
                      (expiration, ref_block_num, ref_block_prefix, max_net_usage_words, max_cpu_usage_ms, delay_sec))
BOOST_DESCRIBE_STRUCT(transaction, (transaction_header), (context_free_actions, actions, transaction_extensions))
BOOST_DESCRIBE_STRUCT(signed_transaction, (transaction), (signatures, context_free_data))
BOOST_DESCRIBE_STRUCT(packed_transaction, (), (signatures, compression, packed_context_free_data, packed_trx))
} // namespace forge::chain::protocol

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::deferred_transaction_generation_context)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::transaction_header)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::transaction)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::signed_transaction)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::packed_transaction)
