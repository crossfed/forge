module;

#include <forge/raw/serialization.hpp>

#include <bit>
#include <cstdint>
#include <new>
#include <variant>
#include <vector>

module forge.chain.block;

import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

namespace forge::chain {

digest block_header::digest() const {
   return block_digest(*this);
}

std::uint32_t block_header::num_from_id(const block_id& id) {
   return calculate_block_num_from_id(id);
}

std::uint32_t block_header::calculate_block_num() const {
   return ::forge::chain::calculate_block_num(*this);
}

block_id block_header::calculate_id() const {
   return calculate_block_id(*this);
}

std::vector<char> signature_preimage(const block_header& value) {
   return forge::raw::pack(value);
}

digest block_digest(const block_header& value) {
   const auto preimage = signature_preimage(value);
   return forge::crypto::sha256::hash(preimage.data(), static_cast<std::uint32_t>(preimage.size()));
}

std::uint32_t calculate_block_num_from_id(const block_id& id) {
   return std::byteswap(static_cast<std::uint32_t>(id._hash[0] & 0xffffffffULL));
}

std::uint32_t calculate_block_num(const block_header& value) {
   return calculate_block_num_from_id(value.previous) + 1U;
}

block_id calculate_block_id(const block_header& value) {
   auto result = block_digest(value);
   result._hash[0] &= 0xffffffff00000000ULL;
   result._hash[0] += std::byteswap(calculate_block_num(value));
   return result;
}

digest transaction_receipt::digest() const {
   return transaction_receipt_digest(*this);
}

digest transaction_receipt_digest(const transaction_receipt& value) {
   digest::encoder encoder;
   forge::raw::pack(encoder, value.status);
   forge::raw::pack(encoder, value.cpu_usage_us);
   forge::raw::pack(encoder, value.net_usage_words);
   if (std::holds_alternative<transaction_id>(value.trx)) {
      forge::raw::pack(encoder, std::get<transaction_id>(value.trx));
   } else {
      forge::raw::pack(encoder, std::get<packed_transaction>(value.trx).packed_digest());
   }
   return encoder.result();
}

digest signed_block::packed_digest() const {
   return signed_block_digest(*this);
}

digest signed_block_digest(const signed_block& value) {
   digest::encoder encoder;
   forge::raw::pack(encoder, static_cast<const signed_block_header&>(value));
   forge::raw::pack(encoder, value.transactions);
   forge::raw::pack(encoder, value.block_extensions);
   return encoder.result();
}

} // namespace forge::chain

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::producer_key)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::producer_schedule)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::block_header)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::signed_block_header)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::transaction_receipt_header)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::transaction_receipt)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::producer_confirmation)
