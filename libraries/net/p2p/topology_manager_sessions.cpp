module;

#include <forge/exceptions/macros.hpp>

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
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>

module forge.net.p2p.node;

import forge.asio.notification;
import forge.net.p2p.discovery;
import forge.net.p2p.exceptions;

#include "details/cancellation_latch.hxx"
#include "details/lifecycle_tracker.hxx"
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
   batch->cancellation = std::make_shared<cancellation_latch>();
   batch->required = required;
   add_cancellation(batch->cancellation);
   const auto executor = co_await boost::asio::this_coro::executor;
   auto launch_failure = std::exception_ptr{};
   for (auto index = std::size_t{}; index < workers; ++index) {
      auto operation = std::shared_ptr<lifecycle_tracker::operation>{};
      auto worker_executor = executor;
      auto lifecycle_stop = std::shared_ptr<lifecycle_stop_source>{};
      auto worker_reserved = false;
      try {
         if (lifecycle_ == nullptr) {
            FORGE_THROW_EXCEPTION(exceptions::internal, "P2P topology dial worker has no lifecycle owner");
         }
         auto tracked = lifecycle_->track();
         if (!tracked.active()) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "P2P topology dial worker lifecycle is stopped");
         }
         operation = std::make_shared<lifecycle_tracker::operation>(std::move(tracked));
         worker_executor = operation->executor();
         lifecycle_stop = operation->stop_source();
         auto self = shared_from_this();
         {
            const auto lock = std::scoped_lock{batch->mutex};
            ++batch->remaining_workers;
            worker_reserved = true;
         }
         boost::asio::co_spawn(
             worker_executor,
             [self, batch, lifecycle_stop]() -> boost::asio::awaitable<void> {
                if (lifecycle_stop && lifecycle_stop->stop_requested()) {
                   co_return;
                }
                co_await self->async_dial_worker(batch);
             },
             [self, batch, operation](std::exception_ptr error) noexcept {
                auto notify = false;
                auto drained = false;
                const auto cancel_batch = static_cast<bool>(error);
                {
                   const auto lock = std::scoped_lock{batch->mutex};
                   if (error && !batch->failure) {
                      batch->failure = error;
                   }
                   batch->admission_closed = batch->admission_closed || cancel_batch;
                   if (batch->remaining_workers != 0) {
                      --batch->remaining_workers;
                   }
                   notify = batch->launches_complete && batch->remaining_workers == 0 &&
                            !std::exchange(batch->completion_notified, true);
                   drained = batch->launches_complete && batch->remaining_workers == 0;
                }
                if (cancel_batch) {
                   batch->cancellation->request_stop();
                }
                if (notify) {
                   batch->completed->notify();
                }
                if (drained) {
                   self->remove_cancellation(batch->cancellation);
                }
                operation->release();
             });
      } catch (...) {
         launch_failure = std::current_exception();
         {
            const auto lock = std::scoped_lock{batch->mutex};
            batch->admission_closed = true;
            if (worker_reserved && batch->remaining_workers != 0) {
               --batch->remaining_workers;
            }
         }
         batch->cancellation->request_stop();
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

   auto join_failure = std::exception_ptr{};
   auto join_complete = false;
   while (!join_complete) {
      try {
         co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
         while (true) {
            const auto observed = batch->completed->epoch();
            {
               const auto lock = std::scoped_lock{batch->mutex};
               if (batch->remaining_workers == 0) {
                  break;
               }
            }
            if (!join_failure && clocks_.before_dial_join_wait) {
               clocks_.before_dial_join_wait();
            }
            static_cast<void>(co_await batch->completed->async_wait(observed));
         }
         join_complete = true;
      } catch (...) {
         if (!join_failure) {
            join_failure = std::current_exception();
         }
         {
            const auto lock = std::scoped_lock{batch->mutex};
            batch->admission_closed = true;
         }
         batch->cancellation->request_stop();
      }
   }
   if (launch_failure) {
      remove_cancellation(batch->cancellation);
      std::rethrow_exception(launch_failure);
   }
   if (join_failure) {
      remove_cancellation(batch->cancellation);
      std::rethrow_exception(join_failure);
   }
   auto failure = std::exception_ptr{};
   {
      const auto lock = std::scoped_lock{batch->mutex};
      failure = batch->failure;
   }
   if (failure) {
      remove_cancellation(batch->cancellation);
      std::rethrow_exception(failure);
   }
   remove_cancellation(batch->cancellation);
}

boost::asio::awaitable<void> topology_manager::async_dial_worker(const std::shared_ptr<dial_batch>& batch) {
   while (!stopping() && !batch->cancellation->stop_requested()) {
      auto candidate = std::optional<discovery::result>{};
      auto claimed = false;
      auto settled = false;
      auto succeeded = false;
      auto dial_failure = std::exception_ptr{};
      auto cancellation = std::shared_ptr<cancellation_latch>{};
      auto root_subscription = cancellation_latch::subscription{};
      auto registered = false;

      const auto settle = [&](bool success) noexcept {
         if (!claimed || settled) {
            return;
         }
         const auto lock = std::scoped_lock{batch->mutex};
         --batch->in_flight;
         if (success) {
            ++batch->successes;
         }
         settled = true;
      };
      const auto fail = [&](const std::exception_ptr& failure) noexcept {
         const auto lock = std::scoped_lock{batch->mutex};
         if (claimed && !settled) {
            --batch->in_flight;
            settled = true;
         }
         batch->admission_closed = true;
         if (!batch->failure) {
            batch->failure = failure;
         }
      };
      const auto cleanup = [&]() noexcept {
         if (cancellation) {
            static_cast<void>(cancellation->finish());
         }
         if (registered) {
            remove_cancellation(cancellation);
            registered = false;
         }
         root_subscription.reset();
      };

      try {
         {
            const auto lock = std::scoped_lock{batch->mutex};
            if (batch->admission_closed || batch->successes >= batch->required ||
                batch->in_flight >= batch->required - batch->successes ||
                batch->next >= batch->candidates.size()) {
               break;
            }
            candidate.emplace(std::move(batch->candidates[batch->next]));
            ++batch->next;
            ++batch->in_flight;
            claimed = true;
         }
         cancellation = std::make_shared<cancellation_latch>();
         root_subscription = cancellation_latch::subscribe(batch->cancellation, [cancellation] noexcept {
            cancellation->request_stop();
         });
         add_cancellation(cancellation);
         registered = true;
         auto dial = callbacks_.dial(*candidate, cancellation);
         try {
            succeeded = co_await std::move(dial);
         } catch (...) {
            dial_failure = std::current_exception();
         }
         settle(succeeded);
         cleanup();
         if (dial_failure && !stopping()) {
            try {
               std::rethrow_exception(dial_failure);
            } catch (...) {
               try {
                  forge::exceptions::capture_and_log("P2P topology dial failed");
               } catch (...) {
               }
            }
         }
         note_dial_result(*candidate, succeeded);
      } catch (...) {
         const auto failure = std::current_exception();
         fail(failure);
         cleanup();
         batch->cancellation->request_stop();
         std::rethrow_exception(failure);
      }
   }
   if (clocks_.before_dial_worker_completion) {
      clocks_.before_dial_worker_completion();
   }
}

} // namespace forge::net::p2p::detail
