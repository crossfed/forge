module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <utility>
#include <vector>

module forge.crypto.bls.signer.configured_provider;

#include "details/configured_provider_impl.hxx"

namespace forge::crypto::bls::signer {

configured_provider::configured_provider(std::unique_ptr<impl> implementation) : impl_(std::move(implementation)) {}

configured_provider::~configured_provider() = default;

std::shared_ptr<configured_provider> configured_provider::create(private_key&& value) {
   return std::shared_ptr<configured_provider>{new configured_provider{std::make_unique<impl>(std::move(value))}};
}

boost::asio::awaitable<description> configured_provider::describe() {
   co_return impl_->identity;
}

boost::asio::awaitable<sign_result> configured_provider::sign(std::vector<std::uint8_t> message) {
   co_return sign_result{
       .key = impl_->identity.key,
       .value = impl_->key.sign(message),
   };
}

} // namespace forge::crypto::bls::signer
