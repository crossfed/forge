module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

module forge.crypto.asymmetric;

import forge.crypto.ed25519;
import forge.crypto.p256;
import forge.crypto.rsa;
import forge.crypto.secp256k1;
import forge.crypto.webauthn;
import forge.exceptions;
import forge.raw.raw;

namespace forge::crypto::asymmetric {
namespace {

template <typename To, typename From> [[nodiscard]] To copy_array(const From& source) {
   static_assert(std::tuple_size_v<To> == std::tuple_size_v<From>);
   auto result = To{};
   std::transform(source.begin(), source.end(), result.begin(),
                  [](auto value) { return static_cast<typename To::value_type>(value); });
   return result;
}

[[nodiscard]] secp256k1::compact_signature compact(const k1_signature& value) {
   return copy_array<secp256k1::compact_signature>(value.data);
}

[[nodiscard]] p256::compact_signature compact(const r1_signature& value) {
   return copy_array<p256::compact_signature>(value.data);
}

[[nodiscard]] k1_signature make_k1_signature(const secp256k1::compact_signature& value) {
   return k1_signature{copy_array<ecc_signature>(value)};
}

[[nodiscard]] r1_signature make_r1_signature(const p256::compact_signature& value) {
   return r1_signature{copy_array<ecc_signature>(value)};
}

template <typename Storage> [[nodiscard]] algorithm private_algorithm(const Storage& storage) noexcept {
   return std::visit(
       []<typename Value>(const Value&) {
          if constexpr (std::same_as<Value, secp256k1::private_key>) {
             return algorithm::secp256k1;
          } else if constexpr (std::same_as<Value, p256::private_key>) {
             return algorithm::p256;
          } else if constexpr (std::same_as<Value, ed25519::private_key>) {
             return algorithm::ed25519;
          } else {
             return algorithm::rsa;
          }
       },
       storage);
}

template <typename Key> [[nodiscard]] std::vector<std::uint8_t> secret_bytes(const Key& value) {
   const auto secret = value.get_secret();
   if constexpr (std::same_as<std::decay_t<decltype(secret)>, sha256>) {
      const auto bytes = secret.to_uint8_span();
      return {bytes.begin(), bytes.end()};
   } else {
      return {secret.begin(), secret.end()};
   }
}

} // namespace

public_key private_key::get_public_key() const {
   return std::visit(
       []<typename Value>(const Value& key) -> public_key {
          if constexpr (std::same_as<Value, secp256k1::private_key>) {
             return k1_public_key{key.get_public_key().serialize()};
          } else if constexpr (std::same_as<Value, p256::private_key>) {
             return r1_public_key{key.get_public_key().serialize()};
          } else if constexpr (std::same_as<Value, ed25519::private_key>) {
             return ed25519_public_key{key.get_public_key().serialize()};
          } else {
             return rsa_public_key{key.get_public_key().serialize()};
          }
       },
       _storage);
}

algorithm private_key::type() const noexcept {
   return private_algorithm(_storage);
}

signature private_key::sign(std::span<const std::uint8_t> message) const {
   return std::visit(
       [&](const auto& key) -> signature {
          using key_type = std::decay_t<decltype(key)>;
          if constexpr (std::same_as<key_type, secp256k1::private_key>) {
             return make_k1_signature(key.sign_compact(sha256::hash(message), true));
          } else if constexpr (std::same_as<key_type, p256::private_key>) {
             return make_r1_signature(key.sign_compact(sha256::hash(message)));
          } else if constexpr (std::same_as<key_type, ed25519::private_key>) {
             return ed25519_signature{key.sign(message)};
          } else {
             return rsa_signature{key.sign(message)};
          }
       },
       _storage);
}

signature private_key::sign_digest(const sha256& digest) const {
   return std::visit(
       [&](const auto& key) -> signature {
          using key_type = std::decay_t<decltype(key)>;
          if constexpr (std::same_as<key_type, secp256k1::private_key>) {
             return make_k1_signature(key.sign_compact(digest, true));
          } else if constexpr (std::same_as<key_type, p256::private_key>) {
             return make_r1_signature(key.sign_compact(digest));
          } else if constexpr (std::same_as<key_type, ed25519::private_key>) {
             return ed25519_signature{key.sign(digest.to_uint8_span())};
          } else {
             return rsa_signature{key.sign(digest.to_uint8_span())};
          }
       },
       _storage);
}

bool operator==(const private_key& left, const private_key& right) {
   if (left._storage.index() != right._storage.index()) {
      return false;
   }
   return std::visit(
       [&](const auto& value) {
          using value_type = std::decay_t<decltype(value)>;
          return secret_bytes(value) == secret_bytes(std::get<value_type>(right._storage));
       },
       left._storage);
}

bool operator<(const private_key& left, const private_key& right) {
   if (left._storage.index() != right._storage.index()) {
      return left._storage.index() < right._storage.index();
   }
   return std::visit(
       [&](const auto& value) {
          using value_type = std::decay_t<decltype(value)>;
          return secret_bytes(value) < secret_bytes(std::get<value_type>(right._storage));
       },
       left._storage);
}

bool valid(const public_key& key) {
   return std::visit(
       [](const auto& value) {
          using value_type = std::decay_t<decltype(value)>;
          if constexpr (std::same_as<value_type, k1_public_key>) {
             return secp256k1::public_key{value.data}.valid();
          } else if constexpr (std::same_as<value_type, r1_public_key>) {
             return p256::public_key{value.data}.valid();
          } else if constexpr (std::same_as<value_type, webauthn_public_key>) {
             return webauthn::valid(value);
          } else if constexpr (std::same_as<value_type, ed25519_public_key>) {
             return ed25519::public_key{value.data}.valid();
          } else {
             return rsa::public_key{value.data}.valid();
          }
       },
       key);
}

bool verify(const public_key& key, std::span<const std::uint8_t> message, const signature& value) {
   if (type(key) != type(value)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "signature algorithm does not match public key");
   }

   return std::visit(
       [&](const auto& typed_key) {
          using key_type = std::decay_t<decltype(typed_key)>;
          if constexpr (std::same_as<key_type, k1_public_key>) {
             return secp256k1::verify_message(secp256k1::public_key{typed_key.data}, message,
                                              compact(std::get<k1_signature>(value)));
          } else if constexpr (std::same_as<key_type, r1_public_key>) {
             return p256::verify_message(p256::public_key{typed_key.data}, message,
                                         compact(std::get<r1_signature>(value)));
          } else if constexpr (std::same_as<key_type, webauthn_public_key>) {
             return webauthn::recover(std::get<webauthn_signature>(value), sha256::hash(message), true) == typed_key;
          } else if constexpr (std::same_as<key_type, ed25519_public_key>) {
             return ed25519::public_key{typed_key.data}.verify(message, std::get<ed25519_signature>(value).data);
          } else {
             return rsa::public_key{typed_key.data}.verify(message, std::get<rsa_signature>(value).data);
          }
       },
       key);
}

public_key recover(const signature& value, const sha256& digest, bool check_canonical) {
   return std::visit(
       [&](const auto& item) -> public_key {
          using value_type = std::decay_t<decltype(item)>;
          if constexpr (std::same_as<value_type, k1_signature>) {
             return k1_public_key{secp256k1::public_key{compact(item), digest, check_canonical}.serialize()};
          } else if constexpr (std::same_as<value_type, r1_signature>) {
             return r1_public_key{p256::public_key{compact(item), digest, check_canonical}.serialize()};
          } else if constexpr (std::same_as<value_type, webauthn_signature>) {
             return webauthn::recover(item, digest, check_canonical);
          } else {
             FORGE_THROW_EXCEPTION(exceptions::invalid_options, "signature type does not support public key recovery");
          }
       },
       value);
}

std::size_t hash_value(const signature& value) {
   auto seed = std::size_t{};
   for (const auto byte : forge::raw::pack(value)) {
      seed ^= static_cast<std::size_t>(byte) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
   }
   return seed;
}

std::ostream& operator<<(std::ostream& stream, const public_key& key) {
   stream << "public_key(" << encoding::forge().format(key) << ')';
   return stream;
}

} // namespace forge::crypto::asymmetric
