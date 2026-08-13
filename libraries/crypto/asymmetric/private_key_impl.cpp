module;
#include <cstdint>
#include <secp256k1.h>
#include <span>

module forge.crypto.asymmetric.secp256k1;

import forge.crypto.core.secret_bytes;

#include "details/private_key_impl.hxx"

namespace forge::crypto::asymmetric::secp256k1::detail {

private_key_impl::private_key_impl() noexcept = default;

private_key_impl::private_key_impl(const private_key_impl& other) noexcept : _key(other._key) {}

private_key_impl::~private_key_impl() {
   forge::crypto::core::secure_erase(
       std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(_key.data()), _key.data_size()});
}

private_key_impl& private_key_impl::operator=(const private_key_impl& other) noexcept {
   _key = other._key;
   return *this;
}

} // namespace forge::crypto::asymmetric::secp256k1::detail
