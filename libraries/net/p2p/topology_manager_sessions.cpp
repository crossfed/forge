module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>

module forge.net.p2p.node;

import forge.asio.notification;
import forge.exceptions;
import forge.net.p2p.discovery;

#include "details/cancellation_latch.hxx"
#include "details/lifecycle_wakeup.hxx"
#include "details/topology_manager.hxx"

namespace forge::net::p2p::detail {

boost::asio::awaitable<void> topology_manager::async_reconcile_sessions() {
   if (stopping()) {
      co_return;
   }
   callbacks_.refresh_connection_scores();
   auto sessions = callbacks_.sessions();
   if (sessions.active_peers < policy_.peers.low) {
      const auto required = policy_.peers.target - sessions.active_peers;
      co_await async_dial_candidates(candidates_for_dial(sessions), required);
      co_return;
   }
   if (sessions.active_peers <= policy_.peers.high) {
      co_return;
   }

   callbacks_.refresh_connection_scores();
   const auto plan = callbacks_.plan_peer_prune(policy_.peers.target, sessions.active_peers - policy_.peers.target,
                                                clocks_.steady_now());
   if (!plan.session_ids.empty()) {
      co_await callbacks_.close_sessions(plan.session_ids);
   }
}

boost::asio::awaitable<void> topology_manager::async_dial_candidates(std::vector<discovery::result> candidates,
                                                                      std::size_t required) {
   if (required == 0 || candidates.empty() || stopping()) {
      co_return;
   }

   const auto workers = std::min(std::min(policy_.max_parallel_dials, candidates.size()), required);
   auto batch = std::make_shared<dial_batch>();
   batch->candidates = std::move(candidates);
   batch->completed = std::make_shared<lifecycle_wakeup>();
   batch->required = required;
   const auto executor = co_await boost::asio::this_coro::executor;
   for (auto index = std::size_t{}; index < workers; ++index) {
      {
         const auto lock = std::scoped_lock{batch->mutex};
         ++batch->remaining_workers;
      }
      try {
         auto self = shared_from_this();
         boost::asio::co_spawn(
             executor,
             [self = std::move(self), batch]() -> boost::asio::awaitable<void> {
                co_await self->async_dial_worker(batch);
             },
             [batch](std::exception_ptr error) {
                auto notify = false;
                {
                   const auto lock = std::scoped_lock{batch->mutex};
                   if (error && !batch->failure) {
                      batch->failure = error;
                   }
                   if (batch->remaining_workers != 0) {
                      --batch->remaining_workers;
                   }
                   notify = batch->launches_complete && batch->remaining_workers == 0 &&
                            !std::exchange(batch->completion_notified, true);
                }
                if (notify) {
                   batch->completed->notify();
                }
             });
      } catch (...) {
         const auto failure = std::current_exception();
         {
            const auto lock = std::scoped_lock{batch->mutex};
            if (!batch->failure) {
               batch->failure = failure;
            }
            --batch->remaining_workers;
         }
         break;
      }
   }

   auto notify = false;
   {
      const auto lock = std::scoped_lock{batch->mutex};
      batch->launches_complete = true;
      notify = batch->remaining_workers == 0 && !std::exchange(batch->completion_notified, true);
   }
   if (notify) {
      batch->completed->notify();
   }

   while (true) {
      const auto observed = batch->completed->epoch();
      {
         const auto lock = std::scoped_lock{batch->mutex};
         if (batch->remaining_workers == 0) {
            break;
         }
      }
      static_cast<void>(co_await batch->completed->async_wait(observed));
   }
   auto failure = std::exception_ptr{};
   {
      const auto lock = std::scoped_lock{batch->mutex};
      failure = batch->failure;
   }
   if (failure) {
      std::rethrow_exception(failure);
   }
}

boost::asio::awaitable<void> topology_manager::async_dial_worker(const std::shared_ptr<dial_batch>& batch) {
   while (!stopping()) {
      auto candidate = std::optional<discovery::result>{};
      {
         const auto lock = std::scoped_lock{batch->mutex};
         if (batch->successes >= batch->required || batch->next >= batch->candidates.size()) {
            co_return;
         }
         candidate = batch->candidates[batch->next++];
      }
      auto cancellation = std::make_shared<cancellation_latch>();
      add_cancellation(cancellation);
      auto succeeded = false;
      try {
         succeeded = co_await callbacks_.dial(*candidate, cancellation);
      } catch (...) {
         if (!stopping()) {
            forge::exceptions::capture_and_log("P2P topology dial failed");
         }
      }
      static_cast<void>(cancellation->finish());
      remove_cancellation(cancellation);
      note_dial_result(*candidate, succeeded);
      if (succeeded) {
         const auto lock = std::scoped_lock{batch->mutex};
         ++batch->successes;
      }
   }
}

} // namespace forge::net::p2p::detail
