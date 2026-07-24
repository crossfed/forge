module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.crypto.signer.plugin;

import forge.crypto.asymmetric;
import forge.crypto.bls;
import forge.plugins.crypto.signer.bls_api;
import forge.plugins.crypto.signer.types;

#include "details/plugin_impl.hxx"
#include "details/bls_api_impl.hxx"

namespace forge::plugins::crypto::signer {

plugin::bls_api_impl::bls_api_impl(std::shared_ptr<impl> state) : state_{std::move(state)} {}

boost::asio::awaitable<bls_description> plugin::bls_api_impl::describe(bls_describe_request value) {
   co_return state_->describe(std::move(value));
}

boost::asio::awaitable<bls_response> plugin::bls_api_impl::sign(bls_sign_request value) {
   co_return state_->sign(std::move(value));
}

} // namespace forge::plugins::crypto::signer
