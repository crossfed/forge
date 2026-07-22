module;
#include <secp256k1.h>

module forge.crypto.asymmetric.secp256k1;

#include "details/private_key_impl.hxx"

namespace forge::crypto::asymmetric::secp256k1::detail {

private_key_impl::private_key_impl() noexcept = default;

private_key_impl::private_key_impl(const private_key_impl& other) noexcept : _key(other._key) {}

private_key_impl& private_key_impl::operator=(const private_key_impl& other) noexcept {
   _key = other._key;
   return *this;
}

} // namespace forge::crypto::asymmetric::secp256k1::detail
