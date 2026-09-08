module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>

export module forge.chain.api.transaction_signer;

export import forge.api.auth.authenticated_caller;
export import forge.chain.api.exceptions;
export import forge.chain.transaction.types;

import forge.api.core.binding;
import forge.api.core.connection;
import forge.api.core.descriptor;
import forge.api.core.dispatcher;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.registry;
import forge.api.core.types;

export namespace forge::chain::api {

class transaction_signer
    : public forge::api::core::contract<transaction_signer,
                                        forge::api::core::surface::local | forge::api::core::surface::remote> {
 public:
   virtual ~transaction_signer() = default;

   virtual boost::asio::awaitable<chain::transaction::prepared_transaction>
   sign(chain::transaction::unsigned_transaction transaction, forge::api::auth::authenticated_caller caller) = 0;
};

} // namespace forge::chain::api

export namespace forge::api::core {

template <> struct method_descriptor_customization<::forge::chain::api::transaction_signer> {
   template <auto Method, bool EnableRaw>
   static void apply(method_builder<::forge::chain::api::transaction_signer, EnableRaw>& method) {
      static_cast<void>(Method);
      ::forge::chain::api::exceptions::descriptor::declare_signing(method);
   }
};

} // namespace forge::api::core

FORGE_EXPORT_API(::forge::chain::api::transaction_signer,
                 FORGE_API_CONTRACT("forge.chain.api.transaction_signer", 1, 0),
                 FORGE_API_METHOD(sign, transaction, caller))
