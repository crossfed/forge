module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <coroutine>
#include <memory>
#include <utility>

module package.chain_api_component.read_e2e_p2p_info_block_server;

import package.chain_api_component.test_support;
import forge.api.core.binding;
import forge.api.core.registry;
import forge.app.application;
import forge.app.application_shell;
import forge.app.plugin_context;
import forge.chain.api.block;
import forge.chain.api.info;
import forge.chain.api.limits;
import forge.chain.protocol.audit;
import forge.chain.protocol.block_query;
import forge.chain.protocol.info;

namespace package_chain_api_component {

namespace {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

class info_service final : public chain_api::info {
 public:
   explicit info_service(std::shared_ptr<read_p2p_info_block_fixture> state) : state_{std::move(state)} {}

   boost::asio::awaitable<protocol::info_response> get(protocol::anchored_request request) override {
      state_->information_audit.store(request.audit, std::memory_order_relaxed);
      state_->information_calls.fetch_add(1U, std::memory_order_relaxed);
      co_return state_->information;
   }

 private:
   std::shared_ptr<read_p2p_info_block_fixture> state_;
};

class block_service final : public chain_api::block {
 public:
   explicit block_service(std::shared_ptr<read_p2p_info_block_fixture> state) : state_{std::move(state)} {}

   boost::asio::awaitable<protocol::block_response> get_block(protocol::block_request) override {
      co_return protocol::block_response{};
   }
   boost::asio::awaitable<protocol::block_header_response> get_header(protocol::block_request) override {
      co_return protocol::block_header_response{};
   }
   boost::asio::awaitable<protocol::block_state_response> get_block_state(protocol::block_request request) override {
      state_->block_audit.store(request.audit, std::memory_order_relaxed);
      state_->block_calls.fetch_add(1U, std::memory_order_relaxed);
      co_return state_->block;
   }
   boost::asio::awaitable<protocol::block_range_response> get_canonical_range(protocol::block_range_request) override {
      co_return protocol::block_range_response{};
   }
   boost::asio::awaitable<protocol::protocol_features_response>
   get_activated_protocol_features(protocol::protocol_features_request) override {
      co_return protocol::protocol_features_response{};
   }
   boost::asio::awaitable<protocol::consensus_parameters_response>
   get_consensus_parameters(protocol::anchored_request) override {
      co_return protocol::consensus_parameters_response{};
   }
   boost::asio::awaitable<protocol::producers_response> get_producers(protocol::producers_request) override {
      co_return protocol::producers_response{};
   }
   boost::asio::awaitable<protocol::producer_schedule_response>
   get_producer_schedule(protocol::anchored_request) override {
      co_return protocol::producer_schedule_response{};
   }
   boost::asio::awaitable<protocol::finalizer_info_response> get_finalizer_info(protocol::anchored_request) override {
      co_return protocol::finalizer_info_response{};
   }

 private:
   std::shared_ptr<read_p2p_info_block_fixture> state_;
};

} // namespace

p2p_publication_callbacks make_p2p_info_block_publication(std::shared_ptr<read_p2p_info_block_fixture> state) {
   return {
       .install = [state](forge::app::application_context& context) -> boost::asio::awaitable<void> {
          context.apis().install<chain_api::info>(chain_api::limited_descriptor<chain_api::info>(package_limits()),
                                                  std::make_shared<info_service>(state));
          context.apis().install<chain_api::block>(chain_api::limited_descriptor<chain_api::block>(package_limits()),
                                                   std::make_shared<block_service>(state));
          co_return;
       },
       .binding =
           [](forge::app::plugin_context& context) {
              return forge::api::core::binding()
                  .serve(context.apis())
                  .export_api<chain_api::info>({.id = {"forge.chain.api.info"}, .major = 2, .min_revision = 0})
                  .export_api<chain_api::block>({.id = {"forge.chain.api.block"}, .major = 2, .min_revision = 0})
                  .build();
           },
   };
}

} // namespace package_chain_api_component
