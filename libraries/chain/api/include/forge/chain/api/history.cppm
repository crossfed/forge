module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>
#include <forge/api/http/macros.hpp>

export module forge.chain.api.history;

import forge.api.core.binding;
import forge.api.core.connection;
import forge.api.core.descriptor;
import forge.api.core.dispatcher;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.registry;
import forge.api.core.types;
import forge.api.http.binding;
import forge.api.http.client_request;
import forge.api.http.mapping;
import forge.api.http.openapi;
import forge.api.http.proxy;
import forge.chain.api.json_schema;
import forge.crypto.asymmetric;
import forge.crypto.digest.sha256;
import forge.net.http.types;
import forge.variant.variant_dynamic_bitset;

export import forge.chain.api.exceptions;
export import forge.chain.protocol.history_query;

export namespace forge::chain::api {

class history
    : public forge::api::core::contract<history, forge::api::core::surface::local | forge::api::core::surface::remote> {
 public:
   virtual ~history() = default;

   virtual boost::asio::awaitable<protocol::transaction_lookup_response>
   get_transaction(protocol::transaction_history_request value) = 0;
   virtual boost::asio::awaitable<protocol::transaction_trace_response>
   get_transaction_trace(protocol::transaction_history_request value) = 0;
   virtual boost::asio::awaitable<protocol::block_traces_response> get_block_traces(protocol::block_request value) = 0;
   virtual boost::asio::awaitable<protocol::account_actions_response>
   get_account_actions(protocol::account_actions_request value) = 0;
};

} // namespace forge::chain::api

export namespace forge::api::core {

template <> struct method_descriptor_customization<::forge::chain::api::history> {
   template <auto Method, bool EnableRaw>
   static void apply(method_builder<::forge::chain::api::history, EnableRaw>& method) {
      ::forge::chain::api::exceptions::descriptor::declare_historical_query<Method>(method);
      method.template error<::forge::chain::api::exceptions::not_found>(
          "not_found", {.status_code = status::not_found, .retryable = false});
   }
};

} // namespace forge::api::core

FORGE_EXPORT_API(::forge::chain::api::history, FORGE_API_CONTRACT("forge.chain.api.history", 1, 0),
                 FORGE_API_METHOD_TYPED(get_transaction, ::forge::chain::protocol::transaction_history_request,
                                        ::forge::chain::protocol::transaction_lookup_response),
                 FORGE_API_METHOD_TYPED(get_transaction_trace, ::forge::chain::protocol::transaction_history_request,
                                        ::forge::chain::protocol::transaction_trace_response),
                 FORGE_API_METHOD_TYPED(get_block_traces, ::forge::chain::protocol::block_request,
                                        ::forge::chain::protocol::block_traces_response),
                 FORGE_API_METHOD_TYPED(get_account_actions, ::forge::chain::protocol::account_actions_request,
                                        ::forge::chain::protocol::account_actions_response))

FORGE_HTTP_API(
    ::forge::chain::api::history,
    FORGE_HTTP_GET(get_transaction,
                   "/v1/chain/history/transactions/{id}?anchor={anchor}&finality_from={finality_from}&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_GET(get_transaction_trace,
                   "/v1/chain/history/transactions/{id}/trace?anchor={anchor}&finality_from={finality_from}"
                   "&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_GET(get_block_traces,
                   "/v1/chain/history/blocks/traces?id={id}&num={num}&finality_from={finality_from}&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_GET(get_account_actions,
                   "/v1/chain/history/accounts/{account}/actions?cursor={cursor}&limit={limit}&reverse={reverse}"
                   "&anchor={anchor}&finality_from={finality_from}&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)))
