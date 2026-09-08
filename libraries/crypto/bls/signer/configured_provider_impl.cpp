module;

#include <utility>

module forge.crypto.bls.signer.configured_provider;

#include "details/configured_provider_impl.hxx"

namespace forge::crypto::bls::signer {

configured_provider::impl::impl(private_key&& value)
    : key{std::move(value)}, identity{.key = key.get_public_key(), .proof_of_possession = key.proof_of_possession()} {}

} // namespace forge::crypto::bls::signer
