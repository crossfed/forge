module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <bit>
#include <cstdint>
#include <deque>
#include <new>
#include <optional>
#include <variant>
#include <vector>

export module forge.chain.block;

export import forge.chain.transaction;
import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.raw.varint;
import forge.variant.value;
import forge.variant.described;

export namespace forge::chain {


struct producer_key {
   account_name producer_name;
   public_key block_signing_key;
};

struct producer_schedule {
   std::uint32_t version = 0;
   std::vector<producer_key> producers;
};

struct block_header {
   block_timestamp timestamp;
   account_name producer;
   std::uint16_t confirmed = 1;
   block_id previous;
   checksum256 transaction_mroot;
   checksum256 action_mroot;
   std::uint32_t schedule_version = 0;
   std::optional<producer_schedule> new_producers;
   extensions header_extensions;

   [[nodiscard]] digest digest() const;
   [[nodiscard]] block_id calculate_id() const;
   [[nodiscard]] std::uint32_t calculate_block_num() const;
   [[nodiscard]] static std::uint32_t num_from_id(const block_id& id);
};

struct signed_block_header : block_header {
   signature producer_signature;
};

struct transaction_receipt_header {
   enum class status : std::uint8_t {
      executed = 0,
      soft_fail = 1,
      hard_fail = 2,
      delayed = 3,
      expired = 4,
   };

   status status = status::hard_fail;
   std::uint32_t cpu_usage_us = 0;
   forge::unsigned_int net_usage_words = 0;
};

struct transaction_receipt : transaction_receipt_header {
   std::variant<transaction_id, packed_transaction> trx;

   [[nodiscard]] digest digest() const;
};

struct signed_block : signed_block_header {
   std::deque<transaction_receipt> transactions;
   extensions block_extensions;

   [[nodiscard]] ::forge::chain::digest packed_digest() const;
};

struct producer_confirmation {
   ::forge::chain::block_id block_id;
   digest block_digest;
   account_name producer;
   signature sig;
};

std::vector<char> signature_preimage(const block_header& value);
digest block_digest(const block_header& value);
block_id calculate_block_id(const block_header& value);
std::uint32_t calculate_block_num_from_id(const block_id& id);
std::uint32_t calculate_block_num(const block_header& value);
digest transaction_receipt_digest(const transaction_receipt& value);
digest signed_block_digest(const signed_block& value);

} // namespace forge::chain

export namespace forge::chain {
BOOST_DESCRIBE_STRUCT(producer_key, (), (producer_name, block_signing_key))
BOOST_DESCRIBE_STRUCT(producer_schedule, (), (version, producers))
BOOST_DESCRIBE_STRUCT(block_header, (), (timestamp, producer, confirmed, previous, transaction_mroot, action_mroot, schedule_version, new_producers, header_extensions))
BOOST_DESCRIBE_STRUCT(signed_block_header, (block_header), (producer_signature))
BOOST_DESCRIBE_STRUCT(transaction_receipt_header, (), (status, cpu_usage_us, net_usage_words))
BOOST_DESCRIBE_STRUCT(transaction_receipt, (transaction_receipt_header), (trx))
BOOST_DESCRIBE_STRUCT(signed_block, (signed_block_header), (transactions, block_extensions))
BOOST_DESCRIBE_STRUCT(producer_confirmation, (), (block_id, block_digest, producer, sig))
}

export namespace forge::raw {

template <typename Stream>
void pack(Stream& stream, const forge::chain::signed_block& value) {
   forge::raw::pack(stream, static_cast<const forge::chain::signed_block_header&>(value));
   forge::raw::pack(stream, value.transactions);
   forge::raw::pack(stream, value.block_extensions);
}

template <typename Stream>
void unpack(Stream& stream, forge::chain::signed_block& value) {
   forge::raw::unpack(stream, static_cast<forge::chain::signed_block_header&>(value));
   forge::raw::unpack(stream, value.transactions);
   forge::raw::unpack(stream, value.block_extensions);
}

template <>
inline void pack<forge::datastream<std::size_t>, forge::chain::signed_block>(
   forge::datastream<std::size_t>& stream,
   const forge::chain::signed_block& value) {
   forge::raw::pack(stream, static_cast<const forge::chain::signed_block_header&>(value));
   forge::raw::pack(stream, value.transactions);
   forge::raw::pack(stream, value.block_extensions);
}

template <>
inline void pack<forge::datastream<std::uint8_t*>, forge::chain::signed_block>(
   forge::datastream<std::uint8_t*>& stream,
   const forge::chain::signed_block& value) {
   forge::raw::pack(stream, static_cast<const forge::chain::signed_block_header&>(value));
   forge::raw::pack(stream, value.transactions);
   forge::raw::pack(stream, value.block_extensions);
}

template <>
inline void unpack<forge::datastream<const std::uint8_t*>, forge::chain::signed_block>(
   forge::datastream<const std::uint8_t*>& stream,
   forge::chain::signed_block& value) {
   forge::raw::unpack(stream, static_cast<forge::chain::signed_block_header&>(value));
   forge::raw::unpack(stream, value.transactions);
   forge::raw::unpack(stream, value.block_extensions);
}

inline std::vector<char> pack(const forge::chain::signed_block& value) {
   forge::datastream<std::size_t> size_stream;
   forge::raw::pack(size_stream, value);

   std::vector<char> out(size_stream.tellp());
   if (!out.empty()) {
      forge::datastream<char*> stream(out.data(), out.size());
      forge::raw::pack(stream, value);
   }
   return out;
}

} // namespace forge::raw

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::producer_key)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::producer_schedule)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::block_header)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::signed_block_header)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::transaction_receipt_header)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::transaction_receipt)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::producer_confirmation)
