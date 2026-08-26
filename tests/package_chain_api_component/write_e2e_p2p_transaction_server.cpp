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
#include <utility>
#include <vector>

module package.chain_api_component.write_e2e_p2p_transaction_server;

import package.chain_api_component.test_support;
import forge.api.core.binding;
import forge.api.core.registry;
import forge.app.application;
import forge.app.application_shell;
import forge.app.plugin_context;
import forge.chain.api.exceptions;
import forge.chain.api.limits;
import forge.chain.api.submission;
import forge.chain.api.transaction;
import forge.chain.protocol.audit;
import forge.chain.protocol.transaction_query;

namespace package_chain_api_component {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

namespace {

class transaction_service final : public chain_api::transaction {
 public:
   explicit transaction_service(std::shared_ptr<write_p2p_transaction_fixture> state) : state_{std::move(state)} {}

   boost::asio::awaitable<protocol::transaction_status_response>
   get_status(protocol::transaction_status_request) override {
      co_return protocol::transaction_status_response{};
   }

   boost::asio::awaitable<protocol::transaction_status_response>
   await_transaction(protocol::transaction_await_request request) override {
      state_->await_started.fetch_add(1U, std::memory_order_release);
      auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
      timer.expires_after(std::chrono::milliseconds{request.timeout_ms});
      try {
         co_await timer.async_wait(boost::asio::use_awaitable);
      } catch (const boost::system::system_error& error) {
         if (error.code() == boost::asio::error::operation_aborted) {
            state_->await_cancellations.fetch_add(1U, std::memory_order_release);
         }
         throw;
      }
      state_->await_deadlines.fetch_add(1U, std::memory_order_release);
      FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::deadline_exceeded,
                            "fixture transaction wait reached its request deadline");
   }

   boost::asio::awaitable<std::vector<protocol::public_key>>
   get_required_keys(protocol::transaction_required_keys_request) override {
      co_return std::vector<protocol::public_key>{};
   }

   boost::asio::awaitable<protocol::transaction_read_only_response>
   compute_transaction(protocol::transaction_read_only_request request) override {
      state_->audit.store(request.audit, std::memory_order_relaxed);
      state_->calls.fetch_add(1U, std::memory_order_relaxed);
      co_return state_->response;
   }

   boost::asio::awaitable<protocol::transaction_read_only_response>
   send_read_only_transaction(protocol::transaction_read_only_request) override {
      co_return protocol::transaction_read_only_response{};
   }

 private:
   std::shared_ptr<write_p2p_transaction_fixture> state_;
};

class submission_service final : public chain_api::submission {
 public:
   explicit submission_service(std::shared_ptr<write_p2p_transaction_fixture> state) : state_{std::move(state)} {}

   boost::asio::awaitable<protocol::transaction_submit_response>
   submit(protocol::transaction_submit_request request) override {
      state_->submission_calls.fetch_add(1U, std::memory_order_relaxed);
      state_->last_submit_timeout_ms.store(request.timeout_ms, std::memory_order_relaxed);
      co_return protocol::transaction_submit_response{.id = request.transaction.id()};
   }

   boost::asio::awaitable<std::vector<protocol::transaction_submit_response>>
   submit_batch(protocol::transaction_submit_batch_request request) override {
      state_->submission_calls.fetch_add(1U, std::memory_order_relaxed);
      state_->last_batch_timeout_ms.store(request.timeout_ms, std::memory_order_relaxed);
      auto responses = std::vector<protocol::transaction_submit_response>{};
      responses.reserve(request.transactions.size());
      for (const auto& transaction : request.transactions) {
         responses.push_back(protocol::transaction_submit_response{.id = transaction.transaction.id()});
      }
      co_return responses;
   }

 private:
   std::shared_ptr<write_p2p_transaction_fixture> state_;
};

} // namespace

p2p_publication_callbacks make_p2p_transaction_publication(std::shared_ptr<write_p2p_transaction_fixture> state) {
   return {
       .install = [state](forge::app::application_context& context) -> boost::asio::awaitable<void> {
          context.apis().install<chain_api::transaction>(
              chain_api::limited_descriptor<chain_api::transaction>(package_limits()),
              std::make_shared<transaction_service>(state));
          context.apis().install<chain_api::submission>(
              chain_api::limited_descriptor<chain_api::submission>(package_limits()),
              std::make_shared<submission_service>(state));
          co_return;
       },
       .binding =
           [](forge::app::plugin_context& context) {
              return forge::api::core::binding()
                  .serve(context.apis())
                  .export_api<chain_api::transaction>(
                      {.id = {"forge.chain.api.transaction"}, .major = 1, .min_revision = 0})
                  .export_api<chain_api::submission>(
                      {.id = {"forge.chain.api.submission"}, .major = 1, .min_revision = 0})
                  .build();
           },
   };
}

} // namespace package_chain_api_component
