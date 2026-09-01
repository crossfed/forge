module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <coroutine>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module package.chain_api_component.write_e2e_p2p_admin_server;

import package.chain_api_component.test_support;
import forge.api.core.binding;
import forge.api.core.registry;
import forge.app.application;
import forge.app.application_shell;
import forge.app.plugin_context;
import forge.chain.api.admin;
import forge.chain.api.limits;
import forge.chain.protocol.admin;

namespace package_chain_api_component {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

namespace {

class admin_service final : public chain_api::admin {
 public:
   explicit admin_service(std::shared_ptr<write_p2p_admin_fixture> state) : state_{std::move(state)} {}

   boost::asio::awaitable<protocol::push_block_response> push_block(protocol::signed_block) override {
      co_return protocol::push_block_response{};
   }
   boost::asio::awaitable<protocol::snapshot_response> create_snapshot(std::string) override {
      co_return protocol::snapshot_response{};
   }
   boost::asio::awaitable<protocol::prune_response> prune(protocol::prune_request request) override {
      state_->error_calls.fetch_add(1U, std::memory_order_relaxed);
      if (request.max_records == 0) {
         throw std::runtime_error{"chain API package test failure"};
      }
      co_return protocol::prune_response{};
   }
   boost::asio::awaitable<protocol::producer_status_response> producer_status(protocol::admin_query) override {
      state_->calls.fetch_add(1U, std::memory_order_relaxed);
      co_return state_->response;
   }
   boost::asio::awaitable<protocol::supported_protocol_features_response>
   supported_protocol_features(protocol::supported_protocol_features_request) override {
      co_return protocol::supported_protocol_features_response{};
   }
   boost::asio::awaitable<protocol::ram_corrections_response>
   account_ram_corrections(protocol::ram_corrections_request) override {
      co_return protocol::ram_corrections_response{};
   }
   boost::asio::awaitable<protocol::unapplied_transactions_response>
   unapplied_transactions(protocol::unapplied_transactions_request) override {
      co_return protocol::unapplied_transactions_response{};
   }
   boost::asio::awaitable<protocol::snapshot_requests_response> snapshot_requests(protocol::admin_query) override {
      co_return protocol::snapshot_requests_response{};
   }
   boost::asio::awaitable<bool> configure_pause(protocol::producer_pause_request) override {
      co_return false;
   }
   boost::asio::awaitable<bool> update_runtime_options(protocol::producer_runtime_options) override {
      co_return false;
   }
   boost::asio::awaitable<bool> update_greylist(protocol::greylist_update_request) override {
      co_return false;
   }
   boost::asio::awaitable<bool> set_access_policy(protocol::producer_access_policy) override {
      co_return false;
   }
   boost::asio::awaitable<protocol::snapshot_schedule> schedule_snapshot(protocol::snapshot_schedule_request) override {
      co_return protocol::snapshot_schedule{};
   }
   boost::asio::awaitable<protocol::snapshot_schedule> unschedule_snapshot(protocol::snapshot_schedule_id) override {
      co_return protocol::snapshot_schedule{};
   }
   boost::asio::awaitable<protocol::integrity_hash_response> integrity_hash(protocol::admin_query) override {
      co_return protocol::integrity_hash_response{};
   }
   boost::asio::awaitable<bool> schedule_protocol_features(std::vector<protocol::digest>) override {
      co_return false;
   }

 private:
   std::shared_ptr<write_p2p_admin_fixture> state_;
};

} // namespace

p2p_publication_callbacks make_p2p_admin_publication(std::shared_ptr<write_p2p_admin_fixture> state) {
   return {
       .install = [state](forge::app::application_context& context) -> boost::asio::awaitable<void> {
          context.apis().install<chain_api::admin>(chain_api::limited_descriptor<chain_api::admin>(package_limits()),
                                                   std::make_shared<admin_service>(state));
          co_return;
       },
       .binding =
           [](forge::app::plugin_context& context) {
              return forge::api::core::binding()
                  .serve(context.apis())
                  .export_api<chain_api::admin>(chain_api::admin::ref())
                  .build();
           },
   };
}

} // namespace package_chain_api_component
