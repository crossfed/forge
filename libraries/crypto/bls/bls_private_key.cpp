module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <cstdint>
#include <span>

module forge.crypto.bls;

import forge.crypto.core.random;
import forge.exceptions;

namespace forge::crypto::bls {

namespace {

[[nodiscard]] bool valid_private_secret(const std::array<std::uint64_t, 4>& secret) noexcept {
   if (std::all_of(secret.begin(), secret.end(), [](auto word) { return word == 0U; })) {
      return false;
   }
   const auto encoded = bls12_381::sk_to_bytes(secret);
   return bls12_381::sk_from_bytes(encoded) == secret;
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
   return private_key{forge::crypto::core::random_bytes(32U)};
}

} // namespace forge::crypto::bls
