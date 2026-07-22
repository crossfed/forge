module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module forge.net.p2p.identity;

import forge.crypto.asymmetric;
import forge.crypto.pki.der;
import forge.crypto.asymmetric.ed25519;
import forge.crypto.asymmetric.p256;
import forge.crypto.asymmetric.rsa;
import forge.crypto.asymmetric.secp256k1;
import forge.exceptions;
import forge.net.p2p.exceptions;

#include "details/identity_signature.hxx"

namespace forge::net::p2p {
namespace {

template <typename Range> [[nodiscard]] std::vector<std::uint8_t> bytes_from_range(const Range& value) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(value.size());
   for (const auto byte : value) {
      out.push_back(static_cast<std::uint8_t>(byte));
   }
   return out;
}

[[noreturn]] void throw_identity(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_identity, std::move(message));
}

[[nodiscard]] forge::crypto::asymmetric::ed25519::public_key_data ed25519_public_key_data(const public_key& key) {
   if (key.data.size() != forge::crypto::asymmetric::ed25519::public_key_data{}.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "invalid Ed25519 public key size");
   }
   auto out = forge::crypto::asymmetric::ed25519::public_key_data{};
   std::copy(key.data.begin(), key.data.end(), out.begin());
   return out;
}

[[nodiscard]] forge::crypto::asymmetric::secp256k1::public_key_data secp256k1_public_key_data(const public_key& key) {
   if (key.data.size() != forge::crypto::asymmetric::secp256k1::public_key_data{}.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "invalid secp256k1 public key size");
   }
   auto out = forge::crypto::asymmetric::secp256k1::public_key_data{};
   std::copy(key.data.begin(), key.data.end(), out.begin());
   return out;
}

} // namespace

} // namespace forge::net::p2p

extern "C++" {
namespace forge::net::p2p {

public_key public_key_from_crypto(const forge::crypto::asymmetric::public_key& key) {
   return std::visit(
       [](const auto& value) -> public_key {
          using value_type = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<value_type, forge::crypto::asymmetric::ed25519_public_key>) {
             return public_key{.type = public_key::type::ed25519, .data = bytes_from_range(value.serialize())};
          } else if constexpr (std::is_same_v<value_type, forge::crypto::asymmetric::rsa_public_key>) {
             return public_key{.type = public_key::type::rsa, .data = value.serialize()};
          } else if constexpr (std::is_same_v<value_type, forge::crypto::asymmetric::k1_public_key>) {
             return public_key{.type = public_key::type::secp256k1, .data = bytes_from_range(value.serialize())};
          } else if constexpr (std::is_same_v<value_type, forge::crypto::asymmetric::r1_public_key>) {
             const auto spki = forge::crypto::pki::der::write_public_key(forge::crypto::asymmetric::public_key{value});
             return public_key{.type = public_key::type::ecdsa, .data = spki};
          } else {
             FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "WebAuthn keys cannot identify a libp2p peer");
          }
       },
       key);
}

forge::crypto::asymmetric::public_key crypto_public_key(const public_key& key) {
   if (key.data.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p public key is empty");
   }

   switch (key.type) {
   case public_key::type::ed25519:
      return forge::crypto::asymmetric::ed25519_public_key{ed25519_public_key_data(key)};
   case public_key::type::rsa:
      return forge::crypto::asymmetric::rsa_public_key{key.data};
   case public_key::type::secp256k1:
      return forge::crypto::asymmetric::k1_public_key{secp256k1_public_key_data(key)};
   case public_key::type::ecdsa: {
      try {
         auto parsed = forge::crypto::pki::der::read_public_key(key.data);
         if (forge::crypto::asymmetric::type(parsed) != forge::crypto::asymmetric::algorithm::p256) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p ECDSA public key must be P-256");
         }
         return parsed;
      } catch (const forge::exceptions::base& error) {
         throw_identity(error.what());
      }
   }
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "unsupported libp2p public key type");
}

std::vector<std::uint8_t> sign_identity(const forge::crypto::asymmetric::private_key& key,
                                        std::span<const std::uint8_t> message) {
   try {
      return key.visit([&](const auto& value) -> std::vector<std::uint8_t> {
         using value_type = std::decay_t<decltype(value)>;
         if constexpr (std::is_same_v<value_type, forge::crypto::asymmetric::secp256k1::private_key>) {
            return forge::crypto::asymmetric::secp256k1::sign_der(value, message);
         } else if constexpr (std::is_same_v<value_type, forge::crypto::asymmetric::p256::private_key>) {
            return forge::crypto::asymmetric::p256::sign_der(value, message);
         } else {
            return bytes_from_range(value.sign(message));
         }
      });
   } catch (const forge::exceptions::base& error) {
      throw_identity(error.what());
   }
}

bool verify_identity_signature(const public_key& key, std::span<const std::uint8_t> message,
                               std::span<const std::uint8_t> signature) {
   try {
      switch (key.type) {
      case public_key::type::ed25519: {
         if (signature.size() != forge::crypto::asymmetric::ed25519::signature_data{}.size()) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "invalid Ed25519 signature size");
         }
         auto value = forge::crypto::asymmetric::ed25519::signature_data{};
         std::copy(signature.begin(), signature.end(), value.begin());
         return forge::crypto::asymmetric::ed25519::public_key{ed25519_public_key_data(key)}.verify(message, value);
      }
      case public_key::type::rsa:
         return forge::crypto::asymmetric::rsa::public_key{key.data}.verify(message, {signature.begin(), signature.end()});
      case public_key::type::secp256k1:
         return forge::crypto::asymmetric::secp256k1::verify_der(
             forge::crypto::asymmetric::secp256k1::public_key{secp256k1_public_key_data(key)}, message, signature);
      case public_key::type::ecdsa: {
         const auto parsed = std::get<forge::crypto::asymmetric::r1_public_key>(crypto_public_key(key));
         return forge::crypto::asymmetric::p256::verify_der(forge::crypto::asymmetric::p256::public_key{parsed.data}, message, signature);
      }
      }
   } catch (const forge::exceptions::base& error) {
      throw_identity(error.what());
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "unsupported libp2p public key type");
}

} // namespace forge::net::p2p
}
