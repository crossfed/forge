module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <coroutine>
#include <memory>
#include <utility>

module package.chain_api_component.read_e2e_p2p_state_server;

import package.chain_api_component.test_support;
import forge.api.core.binding;
import forge.api.core.registry;
import forge.app.application;
import forge.app.application_shell;
import forge.app.plugin_context;
import forge.chain.api.limits;
import forge.chain.api.state;
import forge.chain.protocol.audit;
import forge.chain.protocol.state_query;

namespace package_chain_api_component {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

namespace {

class state_service final : public chain_api::state {
 public:
   explicit state_service(std::shared_ptr<read_p2p_state_fixture> state) : state_{std::move(state)} {}

   boost::asio::awaitable<protocol::account_response> get_account(protocol::account_request) override {
      co_return protocol::account_response{};
   }
   boost::asio::awaitable<protocol::account_changes_response>
   get_account_changes(protocol::account_changes_request) override {
      co_return protocol::account_changes_response{};
   }
   boost::asio::awaitable<protocol::code_response> get_code(protocol::code_request) override {
      co_return protocol::code_response{};
   }
   boost::asio::awaitable<protocol::permission_links_response>
   get_permission_links(protocol::permission_links_request) override {
      co_return protocol::permission_links_response{};
   }
   boost::asio::awaitable<protocol::table_rows_response> get_table_rows(protocol::table_rows_request) override {
      co_return protocol::table_rows_response{};
   }
   boost::asio::awaitable<protocol::table_changes_response>
   get_table_changes(protocol::table_changes_request request) override {
      state_->audit.store(request.audit, std::memory_order_relaxed);
      state_->calls.fetch_add(1U, std::memory_order_relaxed);
      co_return state_->response;
   }
   boost::asio::awaitable<protocol::table_scope_response> get_table_scope(protocol::table_scope_request) override {
      co_return protocol::table_scope_response{};
   }
   boost::asio::awaitable<protocol::currency_balance_response>
   get_currency_balance(protocol::currency_balance_request) override {
      co_return protocol::currency_balance_response{};
   }
   boost::asio::awaitable<protocol::currency_stats_response>
   get_currency_stats(protocol::currency_stats_request) override {
      co_return protocol::currency_stats_response{};
   }
   boost::asio::awaitable<protocol::scheduled_response>
   get_scheduled_transactions(protocol::scheduled_request) override {
      co_return protocol::scheduled_response{};
   }
   boost::asio::awaitable<protocol::authorizers_response>
   get_accounts_by_authorizers(protocol::authorizers_request) override {
      co_return protocol::authorizers_response{};
   }

 private:
   std::shared_ptr<read_p2p_state_fixture> state_;
};

} // namespace

p2p_publication_callbacks make_p2p_state_publication(std::shared_ptr<read_p2p_state_fixture> state) {
   return {
       .install = [state](forge::app::application_context& context) -> boost::asio::awaitable<void> {
          context.apis().install<chain_api::state>(chain_api::limited_descriptor<chain_api::state>(package_limits()),
                                                   std::make_shared<state_service>(state));
          co_return;
       },
       .binding =
           [](forge::app::plugin_context& context) {
              return forge::api::core::binding()
                  .serve(context.apis())
                  .export_api<chain_api::state>({.id = {"forge.chain.api.state"}, .major = 3, .min_revision = 0})
                  .build();
           },
   };
}

} // namespace package_chain_api_component
