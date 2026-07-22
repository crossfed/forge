module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>

#include <string>
#include <utility>

export module forge.plugins.crypto.signer.api;

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;
import forge.crypto.digest.sha256;
import forge.plugins.crypto.signer.types;

export namespace forge::plugins::crypto::signer {

class api : public forge::api::core::contract<api, forge::api::core::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<response> sign(request value) = 0;

   boost::asio::awaitable<response> sign(std::string key_id, std::string purpose, forge::crypto::digest::sha256 digest) {
      co_return co_await sign(request{
         .key_id = std::move(key_id),
         .purpose = std::move(purpose),
         .digest = digest,
      });
   }

   boost::asio::awaitable<response> sign(std::string key_id, forge::crypto::digest::sha256 digest, options value) {
      co_return co_await sign(request{
         .key_id = std::move(key_id),
         .purpose = std::move(value.purpose),
         .digest = digest,
         .required_algorithm = value.required_algorithm,
      });
   }
};

} // namespace forge::plugins::crypto::signer

FORGE_EXPORT_API(::forge::plugins::crypto::signer::api, FORGE_API_CONTRACT("forge.plugins.crypto.signer", 2, 0),
        FORGE_API_METHOD_TYPED(sign, ::forge::plugins::crypto::signer::request,
                             ::forge::plugins::crypto::signer::response))
