module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <compare>
#include <cstdint>
#include <span>

module forge.crypto.bls;

import forge.crypto.core.random;
import forge.crypto.core.secret_bytes;
import forge.exceptions;

namespace forge::crypto::bls {

namespace {

[[nodiscard]] bool valid_private_secret(const std::array<std::uint64_t, 4>& secret) noexcept {
   if (std::all_of(secret.begin(), secret.end(), [](auto word) { return word == 0U; })) {
      return false;
   }
   return bls12_381::scalar::cmp(secret, bls12_381::fp::Q) == std::strong_ordering::less;
}

void require_valid_private_secret(const std::array<std::uint64_t, 4>& secret) {
   if (!valid_private_secret(secret)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_private_key, "BLS private key is invalid");
   }
}

} // namespace

private_key::private_key(std::span<const std::uint8_t> seed) {
   if (seed.size() < 32U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_private_key, "BLS private key seed must contain at least 32 bytes");
   }
   _secret = bls12_381::secret_key(seed);
   require_valid_private_secret(_secret);
}

private_key::~private_key() {
   forge::crypto::core::secure_erase(
       std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(_secret.data()), sizeof(_secret)});
}

private_key::private_key(private_key&& other) noexcept : _secret{other._secret} {
   forge::crypto::core::secure_erase(
       std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(other._secret.data()), sizeof(other._secret)});
}

private_key& private_key::operator=(private_key&& other) noexcept {
   if (this != &other) {
      forge::crypto::core::secure_erase(
          std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(_secret.data()), sizeof(_secret)});
      _secret = other._secret;
      forge::crypto::core::secure_erase(
          std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(other._secret.data()), sizeof(other._secret)});
   }
   return *this;
}

public_key private_key::get_public_key() const {
   require_valid_private_secret(_secret);
   const auto bytes = bls12_381::public_key(_secret).toAffineBytesLE(bls12_381::from_mont::yes);
   return public_key{std::span<const std::uint8_t, public_key::size_bytes>{bytes}};
}

signature private_key::proof_of_possession() const {
   require_valid_private_secret(_secret);
   const auto bytes = bls12_381::pop_prove(_secret).toAffineBytesLE(bls12_381::from_mont::yes);
   return signature{std::span<const std::uint8_t, signature::size_bytes>{bytes}};
}

signature private_key::sign(std::span<const std::uint8_t> message) const {
   require_valid_private_secret(_secret);
   const auto bytes = bls12_381::sign(_secret, message).toAffineBytesLE(bls12_381::from_mont::yes);
   return signature{std::span<const std::uint8_t, signature::size_bytes>{bytes}};
}

private_key private_key::generate() {
   auto seed = forge::crypto::core::secret_bytes{forge::crypto::core::random_bytes(32U)};
   return private_key{seed.span()};
}

} // namespace forge::crypto::bls
