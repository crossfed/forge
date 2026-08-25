module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

module package.chain_api_component.read_fixture;

import forge.chain.api.admin;
import forge.chain.api.block;
import forge.chain.api.info;
import forge.chain.api.limits;
import forge.chain.api.state;
import forge.chain.api.submission;
import forge.chain.api.transaction;
import forge.chain.protocol.audit;
import forge.chain.protocol.block_query;
import forge.chain.protocol.info;
import forge.chain.protocol.state_query;

namespace package_chain_api_component {

namespace {

protocol::audit_class audit_class_for(std::string_view api, std::string_view method) {
   using enum protocol::audit_class;
   if (api == "forge.chain.api.info") {
      return finality;
   }
   if (api == "forge.chain.api.block") {
      return unsupported;
   }
   if (api == "forge.chain.api.state") {
      if (method == "get_table_changes" || method == "get_account_changes") {
         return state_changes;
      }
      return state_range;
   }
   if (api == "forge.chain.api.transaction") {
      return unsupported;
   }
   return none;
}

template <typename Interface> void append_capabilities(protocol::capabilities& result) {
   const auto descriptor = Interface::describe();
   for (const auto& method : descriptor.methods) {
      result.methods.push_back(protocol::method_capability{
          .api = descriptor.id.value,
          .method = method.name,
          .audit = audit_class_for(descriptor.id.value, method.name),
          .enabled = true,
          .http = true,
          .p2p = true,
      });
   }
}

} // namespace

protocol::info_response make_info_response() {
   auto response = make_audited_info_response();
   append_capabilities<chain_api::info>(response.available);
   append_capabilities<chain_api::block>(response.available);
   append_capabilities<chain_api::state>(response.available);
   append_capabilities<chain_api::transaction>(response.available);
   append_capabilities<chain_api::submission>(response.available);
   append_capabilities<chain_api::admin>(response.available);
   response.limits = package_limits();
   chain_api::require_response_within_limits(response, response.limits);
   return response;
}

protocol::block_state_response make_block_state_response(const protocol::info_response& source) {
   auto response = protocol::block_state_response{};
   response.context = source.context;
   response.audit = source.audit;
   response.id = hash("chain-api-e2e-block-state");
   response.num = 40;
   response.state = {0x31, 0x32, 0x33};
   return response;
}

protocol::table_changes_response make_table_changes_response(const protocol::info_response& source) {
   auto response = protocol::table_changes_response{};
   response.context = source.context;
   response.audit = source.audit;
   if (response.audit) {
      response.audit->state = {protocol::proof_blob{
          .scheme = "forge.db.authenticated.changes",
          .version = 1,
          .payload = {0x10, 0x11, 0x12, 0x13},
      }};
   }
   response.blocks = {{
       .anchor = *source.context.anchor,
       .mutations = {{
           .table = {.code = protocol::account_name{"tester"},
                     .scope = protocol::name{"scope"},
                     .table = protocol::name{"rows"}},
           .primary = 7U,
           .row = protocol::table_row{.value = {0x41, 0x42, 0x43}},
       }},
   }};
   return response;
}

info_implementation::info_implementation(protocol::info_response response) : response_{std::move(response)} {}

boost::asio::awaitable<protocol::info_response> info_implementation::get(protocol::anchored_request request) {
   last_audit.store(request.audit, std::memory_order_relaxed);
   calls.fetch_add(1, std::memory_order_relaxed);
   co_return response_;
}

block_implementation::block_implementation(protocol::block_state_response response) : response_{std::move(response)} {}

boost::asio::awaitable<protocol::block_response> block_implementation::get_block(protocol::block_request) {
   co_return protocol::block_response{};
}

boost::asio::awaitable<protocol::block_header_response> block_implementation::get_header(protocol::block_request) {
   co_return protocol::block_header_response{};
}

boost::asio::awaitable<protocol::block_state_response>
block_implementation::get_block_state(protocol::block_request request) {
   last_audit.store(request.audit, std::memory_order_relaxed);
   calls.fetch_add(1, std::memory_order_relaxed);
   co_return response_;
}

boost::asio::awaitable<protocol::block_range_response>
block_implementation::get_canonical_range(protocol::block_range_request) {
   co_return protocol::block_range_response{};
}

boost::asio::awaitable<protocol::protocol_features_response>
block_implementation::get_activated_protocol_features(protocol::protocol_features_request) {
   co_return protocol::protocol_features_response{};
}

boost::asio::awaitable<protocol::consensus_parameters_response>
block_implementation::get_consensus_parameters(protocol::anchored_request) {
   co_return protocol::consensus_parameters_response{};
}

boost::asio::awaitable<protocol::producers_response> block_implementation::get_producers(protocol::producers_request) {
   co_return protocol::producers_response{};
}

boost::asio::awaitable<protocol::producer_schedule_response>
block_implementation::get_producer_schedule(protocol::anchored_request) {
   co_return protocol::producer_schedule_response{};
}

boost::asio::awaitable<protocol::finalizer_info_response>
block_implementation::get_finalizer_info(protocol::anchored_request) {
   co_return protocol::finalizer_info_response{};
}

state_implementation::state_implementation(protocol::table_changes_response response) : response_{std::move(response)} {}

boost::asio::awaitable<protocol::account_response> state_implementation::get_account(protocol::account_request) {
   co_return protocol::account_response{};
}

boost::asio::awaitable<protocol::account_changes_response>
state_implementation::get_account_changes(protocol::account_changes_request) {
   co_return protocol::account_changes_response{};
}

boost::asio::awaitable<protocol::code_response> state_implementation::get_code(protocol::code_request) {
   co_return protocol::code_response{};
}

boost::asio::awaitable<protocol::permission_links_response>
state_implementation::get_permission_links(protocol::permission_links_request) {
   co_return protocol::permission_links_response{};
}

boost::asio::awaitable<protocol::table_rows_response> state_implementation::get_table_rows(protocol::table_rows_request) {
   co_return protocol::table_rows_response{};
}

boost::asio::awaitable<protocol::table_changes_response>
state_implementation::get_table_changes(protocol::table_changes_request request) {
   last_audit.store(request.audit, std::memory_order_relaxed);
   calls.fetch_add(1, std::memory_order_relaxed);
   co_return response_;
}

boost::asio::awaitable<protocol::table_scope_response> state_implementation::get_table_scope(protocol::table_scope_request) {
   co_return protocol::table_scope_response{};
}

boost::asio::awaitable<protocol::currency_balance_response>
state_implementation::get_currency_balance(protocol::currency_balance_request) {
   co_return protocol::currency_balance_response{};
}

boost::asio::awaitable<protocol::currency_stats_response>
state_implementation::get_currency_stats(protocol::currency_stats_request) {
   co_return protocol::currency_stats_response{};
}

boost::asio::awaitable<protocol::scheduled_response>
state_implementation::get_scheduled_transactions(protocol::scheduled_request) {
   co_return protocol::scheduled_response{};
}

boost::asio::awaitable<protocol::authorizers_response>
state_implementation::get_accounts_by_authorizers(protocol::authorizers_request) {
   co_return protocol::authorizers_response{};
}

read_services::read_services(std::shared_ptr<info_implementation> information, std::shared_ptr<block_implementation> blocks,
                             std::shared_ptr<state_implementation> state)
    : information_{std::move(information)}, blocks_{std::move(blocks)}, state_{std::move(state)} {}

std::shared_ptr<chain_api::info> read_services::information() const {
   return information_;
}

std::shared_ptr<chain_api::block> read_services::blocks() const {
   return blocks_;
}

std::shared_ptr<chain_api::state> read_services::state() const {
   return state_;
}

std::uint32_t read_services::information_calls() const {
   return information_->calls.load(std::memory_order_relaxed);
}

std::uint32_t read_services::block_calls() const {
   return blocks_->calls.load(std::memory_order_relaxed);
}

std::uint32_t read_services::state_calls() const {
   return state_->calls.load(std::memory_order_relaxed);
}

bool read_services::information_audit_required() const {
   return information_->last_audit.load(std::memory_order_relaxed) == protocol::audit_mode::required;
}

bool read_services::block_audit_required() const {
   return blocks_->last_audit.load(std::memory_order_relaxed) == protocol::audit_mode::required;
}

bool read_services::state_audit_required() const {
   return state_->last_audit.load(std::memory_order_relaxed) == protocol::audit_mode::required;
}

read_expectations make_read_expectations() {
   auto expectations = read_expectations{};
   expectations.information = make_info_response();
   expectations.block = make_block_state_response(expectations.information);
   expectations.state = make_table_changes_response(expectations.information);
   return expectations;
}

read_services make_read_services(const read_expectations& expectations) {
   return read_services{
       std::make_shared<info_implementation>(expectations.information),
       std::make_shared<block_implementation>(expectations.block),
       std::make_shared<state_implementation>(expectations.state),
   };
}

} // namespace package_chain_api_component
