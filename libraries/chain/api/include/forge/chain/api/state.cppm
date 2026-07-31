module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>
#include <forge/api/http/macros.hpp>

export module forge.chain.api.state;

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
import forge.raw.varint;
import forge.variant.variant_dynamic_bitset;

export import forge.chain.protocol.state_query;

export namespace forge::chain::api {

class state
    : public forge::api::core::contract<state, forge::api::core::surface::local | forge::api::core::surface::remote> {
 public:
   virtual ~state() = default;

   virtual boost::asio::awaitable<protocol::state_point_response> get_point(protocol::state_point_request value) = 0;
   virtual boost::asio::awaitable<protocol::state_range_response> get_range(protocol::state_range_request value) = 0;
   virtual boost::asio::awaitable<protocol::state_changes_response>
   get_changes(protocol::state_changes_request value) = 0;
   virtual boost::asio::awaitable<protocol::account_response> get_account(protocol::account_request value) = 0;
   virtual boost::asio::awaitable<protocol::code_response> get_code(protocol::code_request value) = 0;
   virtual boost::asio::awaitable<protocol::table_rows_response> get_table_rows(protocol::table_rows_request value) = 0;
   virtual boost::asio::awaitable<protocol::table_scope_response>
   get_table_scope(protocol::table_scope_request value) = 0;
   virtual boost::asio::awaitable<protocol::currency_balance_response>
   get_currency_balance(protocol::currency_balance_request value) = 0;
   virtual boost::asio::awaitable<protocol::currency_stats_response>
   get_currency_stats(protocol::currency_stats_request value) = 0;
   virtual boost::asio::awaitable<protocol::scheduled_response>
   get_scheduled_transactions(protocol::scheduled_request value) = 0;
   virtual boost::asio::awaitable<protocol::authorizers_response>
   get_accounts_by_authorizers(protocol::authorizers_request value) = 0;
};

} // namespace forge::chain::api

FORGE_EXPORT_API(::forge::chain::api::state, FORGE_API_CONTRACT("forge.chain.api.state", 1, 0),
                 FORGE_API_METHOD_TYPED(get_point, ::forge::chain::protocol::state_point_request,
                                        ::forge::chain::protocol::state_point_response),
                 FORGE_API_METHOD_TYPED(get_range, ::forge::chain::protocol::state_range_request,
                                        ::forge::chain::protocol::state_range_response),
                 FORGE_API_METHOD_TYPED(get_changes, ::forge::chain::protocol::state_changes_request,
                                        ::forge::chain::protocol::state_changes_response),
                 FORGE_API_METHOD_TYPED(get_account, ::forge::chain::protocol::account_request,
                                        ::forge::chain::protocol::account_response),
                 FORGE_API_METHOD_TYPED(get_code, ::forge::chain::protocol::code_request,
                                        ::forge::chain::protocol::code_response),
                 FORGE_API_METHOD_TYPED(get_table_rows, ::forge::chain::protocol::table_rows_request,
                                        ::forge::chain::protocol::table_rows_response),
                 FORGE_API_METHOD_TYPED(get_table_scope, ::forge::chain::protocol::table_scope_request,
                                        ::forge::chain::protocol::table_scope_response),
                 FORGE_API_METHOD_TYPED(get_currency_balance, ::forge::chain::protocol::currency_balance_request,
                                        ::forge::chain::protocol::currency_balance_response),
                 FORGE_API_METHOD_TYPED(get_currency_stats, ::forge::chain::protocol::currency_stats_request,
                                        ::forge::chain::protocol::currency_stats_response),
                 FORGE_API_METHOD_TYPED(get_scheduled_transactions, ::forge::chain::protocol::scheduled_request,
                                        ::forge::chain::protocol::scheduled_response),
                 FORGE_API_METHOD_TYPED(get_accounts_by_authorizers, ::forge::chain::protocol::authorizers_request,
                                        ::forge::chain::protocol::authorizers_response))

FORGE_HTTP_API(
    ::forge::chain::api::state, FORGE_HTTP_POST(get_point, "/v1/chain/state/point", ok, FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_POST(get_range, "/v1/chain/state/range", ok, FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_POST(get_changes, "/v1/chain/state/changes", ok, FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_GET(get_account, "/v1/chain/state/accounts/{account}?anchor={anchor}&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_GET(get_code,
                   "/v1/chain/state/accounts/{account}/code?include_code={include_code}&include_abi={include_abi}"
                   "&known_abi_hash={known_abi_hash}&anchor={anchor}&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_GET(get_table_rows,
                   "/v1/chain/state/tables/{code}/{scope}/{table}/rows?json={json}&table_key={table_key}"
                   "&lower_bound={lower_bound}&upper_bound={upper_bound}&index_position={index_position}"
                   "&key_type={key_type}&encode_type={encode_type}&limit={limit}&reverse={reverse}"
                   "&show_payer={show_payer}&anchor={anchor}&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_GET(get_table_scope,
                   "/v1/chain/state/tables/{code}/scopes?table={table}&lower_bound={lower_bound}"
                   "&upper_bound={upper_bound}&limit={limit}&reverse={reverse}&anchor={anchor}&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_GET(get_currency_balance,
                   "/v1/chain/state/currencies/{code}/balances/{account}?symbol={symbol}&anchor={anchor}&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_GET(get_currency_stats, "/v1/chain/state/currencies/{code}/stats/{symbol}?anchor={anchor}&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_GET(get_scheduled_transactions,
                   "/v1/chain/state/scheduled-transactions?lower_bound={lower_bound}&limit={limit}&json={json}"
                   "&anchor={anchor}&audit={audit}",
                   FORGE_HTTP_CACHE(no_store)),
    FORGE_HTTP_POST(get_accounts_by_authorizers, "/v1/chain/state/accounts-by-authorizers", ok,
                    FORGE_HTTP_CACHE(no_store)))
