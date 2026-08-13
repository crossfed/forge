module;

#include <utility>

module forge.crypto.signer.configured_provider;

#include "details/configured_provider_impl.hxx"

namespace forge::crypto::signer {

configured_provider::impl::impl(key_id value_id, asymmetric::private_key value_private_key,
                                asymmetric::public_key value_public_key)
    : id{std::move(value_id)}, private_key{std::move(value_private_key)}, public_key{std::move(value_public_key)} {}

} // namespace forge::crypto::signer
