module;

#include <forge/exceptions/macros.hpp>
#include <forge/raw/serialization.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module forge.chain.transaction;

import forge.compression.zlib;
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

using packed_compression = decltype(std::declval<const packed_transaction&>().compression);

[[noreturn]] void fail_unknown_compression() {
   FORGE_THROW_EXCEPTION(forge::compression::exceptions::invalid_input, "unknown packed transaction compression");
}

template <typename T> void append_raw(bytes& out, const T& value) {
   auto bytes = forge::raw::pack(value);
   out.insert(out.end(), bytes.begin(), bytes.end());
}

bytes maybe_compress(bytes value, packed_compression selected_compression) {
   switch (selected_compression) {
      case packed_compression::none:
         return value;
      case packed_compression::zlib:
         return forge::compression::zlib_compress(value, forge::compression::zlib_level::best_compression);
   }
   fail_unknown_compression();
}

bytes maybe_decompress(const bytes& value, packed_compression selected_compression) {
   switch (selected_compression) {
      case packed_compression::none:
         return value;
      case packed_compression::zlib:
         return forge::compression::zlib_decompress(value);
   }
   fail_unknown_compression();
}

bytes pack_context_free_data(const std::vector<bytes>& value, packed_compression selected_compression) {
   if (value.empty()) {
      return {};
   }
   return maybe_compress(forge::raw::pack(value), selected_compression);
}

std::vector<bytes> unpack_context_free_data(const bytes& value, packed_compression selected_compression) {
   if (value.empty()) {
      return {};
   }
   return forge::raw::unpack<std::vector<bytes>>(maybe_decompress(value, selected_compression));
}

bytes pack_transaction_payload(const transaction& value, packed_compression selected_compression) {
   return maybe_compress(forge::raw::pack(value), selected_compression);
}

transaction unpack_transaction_payload(const bytes& value, packed_compression selected_compression) {
   return forge::raw::unpack<transaction>(maybe_decompress(value, selected_compression));
}

} // namespace

bytes pack_transaction(const transaction& value) {
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
   return forge::crypto::sha256::hash(std::span<const std::uint8_t>{bytes.data(), bytes.size()});
}

bytes signature_preimage(const chain_id& chain_id, const transaction& value, const std::vector<bytes>& cfd) {
   auto out = bytes{};
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
   return forge::crypto::sha256::hash(std::span<const std::uint8_t>{preimage.data(), preimage.size()});
}

packed_transaction::packed_transaction(const signed_transaction& value, packed_compression selected_compression)
    : signatures(value.signatures), compression(selected_compression),
      packed_context_free_data(pack_context_free_data(value.context_free_data, selected_compression)),
      packed_trx(pack_transaction_payload(static_cast<const transaction&>(value), selected_compression)) {}

packed_transaction::packed_transaction(signed_transaction&& value, packed_compression selected_compression)
    : signatures(value.signatures), compression(selected_compression),
      packed_context_free_data(pack_context_free_data(value.context_free_data, selected_compression)),
      packed_trx(pack_transaction_payload(static_cast<const transaction&>(value), selected_compression)) {}

transaction_id packed_transaction::id() const {
   return calculate_transaction_id(unpack_transaction_payload(packed_trx, compression));
}

signed_transaction packed_transaction::get_signed_transaction() const {
   auto out = signed_transaction{};
   static_cast<transaction&>(out) = unpack_transaction_payload(packed_trx, compression);
   out.signatures = signatures;
   out.context_free_data = unpack_context_free_data(packed_context_free_data, compression);
   return out;
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
