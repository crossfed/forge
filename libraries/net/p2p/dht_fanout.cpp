module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

module forge.net.p2p.node;

import forge.exceptions;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;

#include "details/dht_fanout.hxx"
#include "details/operation_deadline.hxx"

namespace forge::net::p2p::detail::dht_fanout {

boost::asio::awaitable<result> run(boost::asio::io_context& context, request value, operation invoke) {
   namespace asio = boost::asio;
   using completion = std::tuple<peer_id, bool, std::exception_ptr>;
   using completion_channel = asio::experimental::concurrent_channel<void(boost::system::error_code, completion)>;

   if (value.concurrency == 0 || value.success_target == 0 || value.timeout.count() <= 0 || value.operation.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                            "DHT fanout concurrency, success target, timeout and operation must be positive");
   }

   auto unique = std::set<peer_id>{};
   value.peers.erase(std::remove_if(value.peers.begin(), value.peers.end(),
                                    [&](const auto& peer) { return !unique.insert(peer).second; }),
                     value.peers.end());

   const auto started = std::chrono::steady_clock::now();
   const auto operation_name = value.operation;
   auto deadline = operation_deadline{context, value.timeout};
   auto executor = asio::any_io_executor{co_await asio::this_coro::executor};
   auto strand = asio::make_strand(executor);
   auto active = std::make_shared<std::map<peer_id, std::unique_ptr<asio::cancellation_signal>>>();
   auto stop_requested = std::make_shared<bool>(false);
   auto completions = std::make_shared<completion_channel>(strand, value.concurrency);
   auto callable = std::make_shared<operation>(std::move(invoke));

   deadline.arm([strand, active, stop_requested] {
      asio::post(strand, [active, stop_requested] {
         *stop_requested = true;
         for (const auto& [_, cancellation] : *active) {
            try {
               cancellation->emit(asio::cancellation_type::all);
            } catch (...) {
               // Child completion remains the ownership barrier when cancellation delivery fails.
            }
         }
      });
   });

   auto output = result{};
   auto terminal_error = std::exception_ptr{};
   try {
      output = co_await asio::co_spawn(
          strand,
          [value = std::move(value), started, strand, active, stop_requested, completions,
           callable]() mutable -> asio::awaitable<result> {
             auto out = result{};
             auto next = std::size_t{};

             const auto cancel_children = [&active]() noexcept {
                for (const auto& [_, cancellation] : *active) {
                   try {
                      cancellation->emit(asio::cancellation_type::all);
                   } catch (...) {
                      // The completion channel, not successful signal delivery, owns child lifetime.
                   }
                }
             };

             const auto launch = [&](peer_id peer) {
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
                if (elapsed >= value.timeout) {
                   throw_operation_timeout(value.operation);
                }
                const auto remaining = value.timeout - elapsed;
                auto cancellation = std::make_unique<asio::cancellation_signal>();
                const auto cancellation_slot = cancellation->slot();
                active->emplace(peer, std::move(cancellation));
                ++out.attempted;
                try {
                   asio::co_spawn(
                       strand,
                       [callable, completions, peer, remaining]() -> asio::awaitable<void> {
                          auto succeeded = false;
                          auto error = std::exception_ptr{};
                          try {
                             succeeded = co_await (*callable)(peer, remaining);
                          } catch (...) {
                             error = std::current_exception();
                          }
                          static_cast<void>(completions->try_send(boost::system::error_code{},
                                                                  completion{peer, succeeded, std::move(error)}));
                       },
                       asio::bind_cancellation_slot(cancellation_slot, asio::detached));
                } catch (...) {
                   active->erase(peer);
                   throw;
                }
             };

             auto failure = std::exception_ptr{};
             try {
                while (out.succeeded < value.success_target && !*stop_requested) {
                   while (active->size() < value.concurrency && next < value.peers.size()) {
                      launch(value.peers[next++]);
                   }
                   if (active->empty()) {
                      break;
                   }

                   auto [peer, succeeded, error] = co_await completions->async_receive(asio::use_awaitable);
                   active->erase(peer);
                   if (error) {
                      std::rethrow_exception(error);
                   }
                   out.succeeded += static_cast<std::size_t>(succeeded);
                }
             } catch (...) {
                failure = std::current_exception();
             }

             if (!active->empty()) {
                co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});
                cancel_children();
                while (!active->empty()) {
                   auto [peer, _, error] = co_await completions->async_receive(asio::use_awaitable);
                   active->erase(peer);
                }
             }
             if (failure) {
                std::rethrow_exception(failure);
             }
             co_return out;
          },
          asio::use_awaitable);
   } catch (...) {
      terminal_error = std::current_exception();
   }

   const auto completed = deadline.finish();
   if (!completed) {
      throw_operation_timeout(operation_name);
   }
   if (terminal_error) {
      std::rethrow_exception(terminal_error);
   }
   co_return output;
}

} // namespace forge::net::p2p::detail::dht_fanout
