module;

#include <forge/raw/serialization.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module forge.chain.transaction;

import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.raw.varint;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

namespace forge::chain {
namespace {

template <typename T>
void append_raw(std::vector<char>& out, const T& value) {
   auto bytes = forge::raw::pack(value);
   out.insert(out.end(), bytes.begin(), bytes.end());
}

} // namespace

std::vector<char> pack_transaction(const transaction& value) {
   return forge::raw::pack(value);
}

transaction_id transaction::id() const {
   return calculate_transaction_id(*this);
}

digest transaction::sig_digest(const chain_id& chain_id, const std::vector<bytes>& cfd) const {
   return signature_digest(chain_id, *this, cfd);
}

transaction_id calculate_transaction_id(const transaction& value) {
   const auto bytes = pack_transaction(value);
   return forge::crypto::sha256::hash(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

std::vector<char> signature_preimage(const chain_id& chain_id,
                                     const transaction& value,
                                     const std::vector<bytes>& cfd) {
   auto out = std::vector<char>{};
   append_raw(out, chain_id);
   append_raw(out, value);
   if (cfd.empty()) {
      append_raw(out, digest{});
   } else {
      append_raw(out, digest::hash(cfd));
   }
   return out;
}

digest signature_digest(const chain_id& chain_id, const transaction& value, const std::vector<bytes>& cfd) {
   const auto preimage = signature_preimage(chain_id, value, cfd);
   return forge::crypto::sha256::hash(preimage.data(), static_cast<std::uint32_t>(preimage.size()));
}

packed_transaction::packed_transaction(const signed_transaction& value, enum compression selected_compression)
    : signatures(value.signatures)
    , compression(selected_compression)
    , unpacked_trx(value)
    , trx_id(value.id()) {
   if (compression != compression::none) {
      fail_invalid_argument("chain zlib packed_transaction is deferred");
   }
   packed_trx = forge::raw::pack(static_cast<const transaction&>(value));
   packed_context_free_data = forge::raw::pack(value.context_free_data);
}

packed_transaction::packed_transaction(signed_transaction&& value, enum compression selected_compression)
    : signatures(value.signatures)
    , compression(selected_compression)
    , unpacked_trx(std::move(value))
    , trx_id(unpacked_trx.id()) {
   if (compression != compression::none) {
      fail_invalid_argument("chain zlib packed_transaction is deferred");
   }
   packed_trx = forge::raw::pack(static_cast<const transaction&>(unpacked_trx));
   packed_context_free_data = forge::raw::pack(unpacked_trx.context_free_data);
}

digest packed_transaction::packed_digest() const {
   digest::encoder encoder;
   forge::raw::pack(encoder, compression);
   forge::raw::pack(encoder, packed_trx);

   digest::encoder digest_encoder;
   forge::raw::pack(digest_encoder, signatures);
   forge::raw::pack(digest_encoder, packed_context_free_data);
   forge::raw::pack(encoder, digest_encoder.result());

   return encoder.result();
}

} // namespace forge::chain

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::action_base)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::action)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::deferred_transaction_generation_context)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::transaction_header)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::transaction)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::signed_transaction)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::packed_transaction)
