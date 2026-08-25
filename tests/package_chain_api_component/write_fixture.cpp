module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/system_error.hpp>
#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module package.chain_api_component.write_fixture;

import forge.chain.api.admin;
import forge.chain.api.exceptions;
import forge.chain.api.submission;
import forge.chain.api.transaction;
import forge.chain.protocol.admin;
import forge.chain.protocol.audit;
import forge.chain.protocol.info;
import forge.chain.protocol.transaction_query;

namespace package_chain_api_component {

protocol::transaction_read_only_response make_transaction_response(const protocol::info_response& source) {
   auto response = protocol::transaction_read_only_response{};
   response.context = source.context;
   response.audit = source.audit;
   response.id = hash("chain-api-e2e-read-only-transaction");
   return response;
}

protocol::producer_status_response make_admin_response() {
   auto response = protocol::producer_status_response{};
   response.paused = true;
   response.scheduled_protocol_features = {hash("chain-api-e2e-protocol-feature")};
   return response;
}

transaction_implementation::transaction_implementation(protocol::transaction_read_only_response response)
    : response_{std::move(response)} {}

boost::asio::awaitable<protocol::transaction_status_response>
transaction_implementation::get_status(protocol::transaction_status_request) {
   co_return protocol::transaction_status_response{};
}

boost::asio::awaitable<protocol::transaction_status_response>
transaction_implementation::await_transaction(protocol::transaction_await_request request) {
   await_started.fetch_add(1, std::memory_order_release);
   auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
   timer.expires_after(std::chrono::milliseconds{request.timeout_ms});
   try {
      co_await timer.async_wait(boost::asio::use_awaitable);
   } catch (const boost::system::system_error& error) {
      if (error.code() == boost::asio::error::operation_aborted) {
         await_cancellations.fetch_add(1, std::memory_order_release);
      }
      throw;
   }
   await_deadlines.fetch_add(1, std::memory_order_release);
   FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::deadline_exceeded,
                         "fixture transaction wait reached its request deadline");
}

boost::asio::awaitable<std::vector<protocol::public_key>>
transaction_implementation::get_required_keys(protocol::transaction_required_keys_request) {
   co_return std::vector<protocol::public_key>{};
}

boost::asio::awaitable<protocol::transaction_read_only_response>
transaction_implementation::compute_transaction(protocol::transaction_read_only_request request) {
   last_audit.store(request.audit, std::memory_order_relaxed);
   calls.fetch_add(1, std::memory_order_relaxed);
   co_return response_;
}

boost::asio::awaitable<protocol::transaction_read_only_response>
transaction_implementation::send_read_only_transaction(protocol::transaction_read_only_request) {
   co_return protocol::transaction_read_only_response{};
}

boost::asio::awaitable<protocol::transaction_submit_response>
submission_implementation::submit(protocol::transaction_submit_request request) {
   calls.fetch_add(1U, std::memory_order_relaxed);
   last_submit_timeout_ms.store(request.timeout_ms, std::memory_order_relaxed);
   co_return protocol::transaction_submit_response{.id = request.transaction.id()};
}

boost::asio::awaitable<std::vector<protocol::transaction_submit_response>>
submission_implementation::submit_batch(protocol::transaction_submit_batch_request request) {
   calls.fetch_add(1U, std::memory_order_relaxed);
   last_batch_timeout_ms.store(request.timeout_ms, std::memory_order_relaxed);
   auto responses = std::vector<protocol::transaction_submit_response>{};
   responses.reserve(request.transactions.size());
   for (const auto& transaction : request.transactions) {
      responses.push_back(protocol::transaction_submit_response{.id = transaction.transaction.id()});
   }
   co_return responses;
}

admin_implementation::admin_implementation(protocol::producer_status_response response) : response_{std::move(response)} {}

boost::asio::awaitable<protocol::push_block_response> admin_implementation::push_block(protocol::signed_block) {
   co_return protocol::push_block_response{};
}

boost::asio::awaitable<protocol::snapshot_response> admin_implementation::create_snapshot(std::string) {
   co_return protocol::snapshot_response{};
}

boost::asio::awaitable<protocol::prune_response> admin_implementation::prune(protocol::prune_request request) {
   error_calls.fetch_add(1, std::memory_order_relaxed);
   if (request.max_records == 0) {
      throw std::runtime_error{"chain API package test failure"};
   }
   co_return protocol::prune_response{};
}

boost::asio::awaitable<protocol::producer_status_response>
admin_implementation::producer_status(protocol::admin_query) {
   calls.fetch_add(1, std::memory_order_relaxed);
   co_return response_;
}

boost::asio::awaitable<protocol::supported_protocol_features_response>
admin_implementation::supported_protocol_features(protocol::supported_protocol_features_request) {
   co_return protocol::supported_protocol_features_response{};
}

boost::asio::awaitable<protocol::ram_corrections_response>
admin_implementation::account_ram_corrections(protocol::ram_corrections_request) {
   co_return protocol::ram_corrections_response{};
}

boost::asio::awaitable<protocol::unapplied_transactions_response>
admin_implementation::unapplied_transactions(protocol::unapplied_transactions_request) {
   co_return protocol::unapplied_transactions_response{};
}

boost::asio::awaitable<protocol::snapshot_requests_response>
admin_implementation::snapshot_requests(protocol::admin_query) {
   co_return protocol::snapshot_requests_response{};
}

boost::asio::awaitable<bool> admin_implementation::configure_pause(protocol::producer_pause_request) {
   co_return false;
}

boost::asio::awaitable<bool> admin_implementation::update_runtime_options(protocol::producer_runtime_options) {
   co_return false;
}

boost::asio::awaitable<bool> admin_implementation::update_greylist(protocol::greylist_update_request) {
   co_return false;
}

boost::asio::awaitable<bool> admin_implementation::set_access_policy(protocol::producer_access_policy) {
   co_return false;
}

boost::asio::awaitable<protocol::snapshot_schedule>
admin_implementation::schedule_snapshot(protocol::snapshot_schedule_request) {
   co_return protocol::snapshot_schedule{};
}

boost::asio::awaitable<protocol::snapshot_schedule>
admin_implementation::unschedule_snapshot(protocol::snapshot_schedule_id) {
   co_return protocol::snapshot_schedule{};
}

boost::asio::awaitable<protocol::integrity_hash_response>
admin_implementation::integrity_hash(protocol::admin_query) {
   co_return protocol::integrity_hash_response{};
}

boost::asio::awaitable<bool> admin_implementation::schedule_protocol_features(std::vector<protocol::digest>) {
   co_return false;
}

write_services::write_services(std::shared_ptr<transaction_implementation> transactions,
                               std::shared_ptr<submission_implementation> submissions,
                               std::shared_ptr<admin_implementation> administration)
    : transactions_{std::move(transactions)}, submissions_{std::move(submissions)}, administration_{std::move(administration)} {}

std::shared_ptr<chain_api::transaction> write_services::transactions() const {
   return transactions_;
}

std::shared_ptr<chain_api::submission> write_services::submissions() const {
   return submissions_;
}

std::shared_ptr<chain_api::admin> write_services::administration() const {
   return administration_;
}

std::uint32_t write_services::transaction_calls() const {
   return transactions_->calls.load(std::memory_order_relaxed);
}

std::uint32_t write_services::transaction_await_started() const {
   return transactions_->await_started.load(std::memory_order_acquire);
}

std::uint32_t write_services::transaction_await_deadlines() const {
   return transactions_->await_deadlines.load(std::memory_order_acquire);
}

std::uint32_t write_services::transaction_await_cancellations() const {
   return transactions_->await_cancellations.load(std::memory_order_acquire);
}

std::uint32_t write_services::submission_calls() const {
   return submissions_->calls.load(std::memory_order_acquire);
}

std::uint64_t write_services::submission_last_timeout_ms() const {
   return submissions_->last_submit_timeout_ms.load(std::memory_order_relaxed);
}

std::uint64_t write_services::submission_last_batch_timeout_ms() const {
   return submissions_->last_batch_timeout_ms.load(std::memory_order_relaxed);
}

std::uint32_t write_services::administration_calls() const {
   return administration_->calls.load(std::memory_order_relaxed);
}

std::uint32_t write_services::administration_error_calls() const {
   return administration_->error_calls.load(std::memory_order_relaxed);
}

bool write_services::transaction_audit_required() const {
   return transactions_->last_audit.load(std::memory_order_relaxed) == protocol::audit_mode::required;
}

write_expectations make_write_expectations() {
   const auto source = make_audited_info_response();
   return {
       .transaction = make_transaction_response(source),
       .administration = make_admin_response(),
   };
}

write_services make_write_services(const write_expectations& expectations) {
   return write_services{
       std::make_shared<transaction_implementation>(expectations.transaction),
       std::make_shared<submission_implementation>(),
       std::make_shared<admin_implementation>(expectations.administration),
   };
}

} // namespace package_chain_api_component
