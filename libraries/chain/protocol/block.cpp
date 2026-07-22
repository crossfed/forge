module;

#include <forge/raw/serialization.hpp>

#include <bit>
#include <cstdint>
#include <deque>
#include <new>
#include <span>
#include <variant>
#include <vector>

module forge.chain.protocol.block;

import forge.chain.core.merkle;
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

namespace forge::chain::protocol {

core::digest block_header::digest() const {
   return block_digest(*this);
}

std::uint32_t block_header::num_from_id(const block_id& id) {
   return calculate_block_num_from_id(id);
}

std::uint32_t block_header::calculate_block_num() const {
   return ::forge::chain::protocol::calculate_block_num(*this);
}

block_id block_header::calculate_id() const {
   return calculate_block_id(*this);
}

bytes signature_preimage(const block_header& value) {
   return forge::raw::pack(value);
}

core::digest block_digest(const block_header& value) {
   const auto preimage = signature_preimage(value);
   return forge::crypto::digest::sha256::hash(std::span<const std::uint8_t>{preimage.data(), preimage.size()});
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

core::digest transaction_receipt::digest() const {
   return transaction_receipt_digest(*this);
}

core::digest transaction_receipt_digest(const transaction_receipt& value) {
   core::digest::encoder encoder;
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

core::digest calculate_transaction_mroot(const std::deque<transaction_receipt>& receipts) {
   auto digests = std::vector<core::digest>{};
   digests.reserve(receipts.size());
   for (const auto& receipt : receipts) {
      digests.push_back(transaction_receipt_digest(receipt));
   }
   return core::calculate_merkle_root(digests);
}

core::digest signed_block::packed_digest() const {
   return signed_block_digest(*this);
}

core::digest signed_block_digest(const signed_block& value) {
   core::digest::encoder encoder;
   forge::raw::pack(encoder, static_cast<const signed_block_header&>(value));
   forge::raw::pack(encoder, value.transactions);
   forge::raw::pack(encoder, value.block_extensions);
   return encoder.result();
}

} // namespace forge::chain::protocol

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::block_header)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::signed_block_header)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::transaction_receipt_header)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::transaction_receipt)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::producer_confirmation)
