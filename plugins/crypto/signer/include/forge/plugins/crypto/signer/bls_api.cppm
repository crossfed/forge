module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

export module forge.plugins.crypto.signer.bls_api;

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;
import forge.plugins.crypto.signer.types;

export namespace forge::plugins::crypto::signer {

class bls_api : public forge::api::core::contract<bls_api, forge::api::core::surface::local> {
 public:
   using contract_type = forge::api::core::contract<bls_api, forge::api::core::surface::local>;
   using contract_type::describe;

   virtual ~bls_api() = default;

   virtual boost::asio::awaitable<bls_description> describe(bls_describe_request value) = 0;
   virtual boost::asio::awaitable<bls_response> sign(bls_sign_request value) = 0;

   boost::asio::awaitable<bls_description> describe(std::string key_id, std::string purpose) {
      co_return co_await describe(bls_describe_request{
          .key_id = std::move(key_id),
          .purpose = std::move(purpose),
      });
   }

   boost::asio::awaitable<bls_response> sign(std::string key_id, std::string purpose,
                                             std::vector<std::uint8_t> message) {
      co_return co_await sign(bls_sign_request{
          .key_id = std::move(key_id),
          .purpose = std::move(purpose),
          .message = std::move(message),
      });
   }
};

} // namespace forge::plugins::crypto::signer

FORGE_EXPORT_API(::forge::plugins::crypto::signer::bls_api, FORGE_API_CONTRACT("forge.plugins.crypto.signer.bls", 1, 0),
                 FORGE_API_METHOD_TYPED(describe, ::forge::plugins::crypto::signer::bls_describe_request,
                                        ::forge::plugins::crypto::signer::bls_description),
                 FORGE_API_METHOD_TYPED(sign, ::forge::plugins::crypto::signer::bls_sign_request,
                                        ::forge::plugins::crypto::signer::bls_response))
