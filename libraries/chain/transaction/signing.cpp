module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <coroutine>
#include <utility>

module forge.chain.transaction.signing;

import forge.crypto.asymmetric;

namespace forge::chain::transaction {

namespace {

bool signature_matches(const chain::protocol::public_key& key, const chain::protocol::signature& signature,
                       const chain::protocol::digest& digest) try {
   if (crypto::asymmetric::type(key) != crypto::asymmetric::type(signature)) {
      return false;
   }
   const auto algorithm = crypto::asymmetric::type(key);
   if (algorithm == crypto::asymmetric::algorithm::secp256k1 || algorithm == crypto::asymmetric::algorithm::p256 ||
       algorithm == crypto::asymmetric::algorithm::webauthn) {
      return crypto::asymmetric::recover(signature, digest) == key;
   }
   return crypto::asymmetric::verify(key, digest.to_uint8_span(), signature);
} catch (const forge::exceptions::base&) {
   return false;
}

} // namespace

prepared_transaction pack(chain::protocol::signed_transaction value, compression_type compression) {
   auto packed = chain::protocol::packed_transaction{value, compression};
   auto id = packed.id();
   return prepared_transaction{
       .signed_value = std::move(value),
       .packed = std::move(packed),
       .id = id,
   };
}

boost::asio::awaitable<prepared_transaction> sign(unsigned_transaction value, std::vector<signing_key> keys,
                                                  crypto::signer::provider& signer) {
   if (keys.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "transaction signing requires at least one key");
   }
   for (auto index = std::size_t{}; index < keys.size(); ++index) {
      const auto& key = keys[index];
      if (key.id.value.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "transaction signing key id must not be empty");
      }
      for (auto previous = std::size_t{}; previous < index; ++previous) {
         if (keys[previous].id == key.id || keys[previous].public_key == key.public_key) {
            FORGE_THROW_EXCEPTION(exceptions::duplicate_signature, "transaction signing key is duplicated");
         }
      }
   }

   auto signed_value = chain::protocol::signed_transaction{};
   static_cast<chain::protocol::transaction&>(signed_value) = std::move(value.value);
   signed_value.context_free_data = std::move(value.context_free_data);

   const auto digest = signed_value.sig_digest(value.chain, signed_value.context_free_data);
   signed_value.signatures.reserve(keys.size());
   for (const auto& key : keys) {
      auto response = co_await signer.sign_digest(crypto::signer::sign_digest_request{
          .id = key.id,
          .digest = digest,
      });
      if (response.public_key != key.public_key || !signature_matches(key.public_key, response.signature, digest)) {
         FORGE_THROW_EXCEPTION(exceptions::signer_mismatch,
                               "signer returned a key or signature that does not match the requested key");
      }
      signed_value.signatures.push_back(std::move(response.signature));
   }

   co_return pack(std::move(signed_value), value.compression);
}

} // namespace forge::chain::transaction
