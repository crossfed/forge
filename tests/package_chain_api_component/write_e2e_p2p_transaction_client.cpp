module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

module package.chain_api_component.write_e2e_p2p_transaction_client;

import package.chain_api_component.test_support;
import forge.api.core.exceptions;
import forge.api.core.handle;
import forge.asio.exceptions;
import forge.chain.api.exceptions;
import forge.chain.api.submission;
import forge.chain.api.transaction;
import forge.chain.protocol.audit;
import forge.chain.protocol.transaction_query;

namespace package_chain_api_component {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

namespace {

boost::asio::awaitable<void> wait_until(std::function<bool()> predicate, std::string_view failure) {
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
   while (!predicate() && std::chrono::steady_clock::now() < deadline) {
      timer.expires_after(std::chrono::milliseconds{1});
      co_await timer.async_wait(boost::asio::use_awaitable);
   }
   require(predicate(), failure);
}

template <typename Result>
boost::asio::awaitable<void> wait_for_completion(std::future<Result>& pending, std::string_view failure) {
   co_await wait_until([&] { return pending.wait_for(std::chrono::seconds{0}) == std::future_status::ready; }, failure);
}

boost::asio::awaitable<void> require_long_poll_transport(const forge::api::core::handle<chain_api::transaction>& remote,
                                                         const std::shared_ptr<write_p2p_transaction_fixture>& state) {
   const auto deadlines_before = state->await_deadlines.load(std::memory_order_acquire);
   auto deadline_observed = false;
   try {
      static_cast<void>(co_await remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 10}));
   } catch (const forge::chain::api::exceptions::deadline_exceeded&) {
      deadline_observed = true;
   }
   require(deadline_observed, "P2P long-poll ignored its request deadline");
   require(state->await_deadlines.load(std::memory_order_acquire) == deadlines_before + 1U,
           "P2P deadline did not originate at the owner");

   const auto started_before = state->await_started.load(std::memory_order_acquire);
   const auto cancellations_before = state->await_cancellations.load(std::memory_order_acquire);
   auto cancellation = boost::asio::cancellation_signal{};
   const auto executor = co_await boost::asio::this_coro::executor;
   auto pending = boost::asio::co_spawn(
       executor, remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 300'000}),
       boost::asio::bind_cancellation_slot(cancellation.slot(), boost::asio::use_future));
   co_await wait_until([&] { return state->await_started.load(std::memory_order_acquire) > started_before; },
                       "P2P long-poll did not reach the owner");
   cancellation.emit(boost::asio::cancellation_type::all);
   co_await wait_for_completion(pending, "P2P long-poll cancellation did not complete promptly");
   auto caller_cancelled = false;
   try {
      static_cast<void>(pending.get());
   } catch (const forge::api::core::exceptions::cancelled&) {
      caller_cancelled = true;
   } catch (const forge::asio::exceptions::canceled&) {
      caller_cancelled = true;
   } catch (const std::exception& error) {
      require(false, std::string{"P2P long-poll leaked a standard exception: "} + error.what());
   }
   require(caller_cancelled, "P2P long-poll did not return typed cancellation");
   co_await wait_until(
       [&] { return state->await_cancellations.load(std::memory_order_acquire) > cancellations_before; },
       "P2P long-poll cancellation did not reach the owner");
   static_cast<void>(co_await remote->get_status(protocol::transaction_status_request{}));
}

} // namespace

boost::asio::awaitable<write_p2p_transaction_responses>
run_p2p_transaction_client(forge::api::transport::connection connection,
                           std::shared_ptr<write_p2p_transaction_fixture> state) {
   auto transactions = co_await connection.get_remote_api<chain_api::transaction>();
   auto submissions = co_await connection.get_remote_api<chain_api::submission>();
   const auto started_before = state->await_started.load(std::memory_order_acquire);
   auto limit_rejected = false;
   try {
      static_cast<void>(
          co_await transactions->await_transaction(protocol::transaction_await_request{.timeout_ms = 300'001}));
   } catch (const forge::chain::api::exceptions::resource_exhausted&) {
      limit_rejected = true;
   }
   require(limit_rejected, "P2P owner boundary accepted an oversized await deadline");
   require(state->await_started.load(std::memory_order_acquire) == started_before,
           "P2P oversized await deadline reached the owner");

   auto responses = write_p2p_transaction_responses{};
   responses.transaction = co_await transactions->compute_transaction(
       protocol::transaction_read_only_request{.audit = protocol::audit_mode::required});
   auto submitted = protocol::transaction_submit_request{};
   submitted.transaction = protocol::packed_transaction{protocol::signed_transaction{}};
   const auto submitted_id = submitted.transaction.id();
   require((co_await submissions->submit(std::move(submitted))).id == submitted_id,
           "P2P submission acknowledgement did not bind the submitted transaction");
   co_await require_long_poll_transport(transactions, state);
   co_return responses;
}

} // namespace package_chain_api_component
