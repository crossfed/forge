module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>

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

module forge.net.p2p.node;

import forge.asio.notification;
import forge.exceptions;
import forge.net.p2p.discovery;
import forge.net.p2p.exceptions;
import forge.net.p2p.topology;

#include "details/cancellation_latch.hxx"
#include "details/lifecycle_tracker.hxx"
#include "details/lifecycle_wakeup.hxx"
#include "details/topology_manager.hxx"

namespace forge::net::p2p::detail {

topology_manager::topology_manager(topology::policy policy, callbacks callbacks_value, clocks clocks_value)
    : policy_{std::move(policy)}, callbacks_{std::move(callbacks_value)}, clocks_{std::move(clocks_value)},
      changed_{std::make_shared<lifecycle_wakeup>()} {
   forge::net::p2p::validate(policy_);
   if (!clocks_.steady_now) {
      clocks_.steady_now = [] { return std::chrono::steady_clock::now(); };
   }
   if (!clocks_.system_now) {
      clocks_.system_now = [] { return std::chrono::system_clock::now(); };
   }
   const auto point_limit = std::min(policy_.rendezvous_points.size(), policy_.max_rendezvous_points);
   for (auto point_index = std::size_t{}; point_index < point_limit; ++point_index) {
      const auto& point = policy_.rendezvous_points[point_index];
      if (!point.endpoint.peer) {
         continue;
      }
      for (const auto& namespace_name : point.namespaces) {
         rendezvous_clients_.try_emplace(rendezvous_key{
                                             .peer = *point.endpoint.peer,
                                             .namespace_name = namespace_name,
                                         },
                                         rendezvous_state{.point_index = point_index});
      }
   }
}

topology_manager::~topology_manager() {
   request_stop();
}

bool topology_manager::observation_key::operator<(const observation_key& other) const noexcept {
   if (peer != other.peer) {
      return peer < other.peer;
   }
   return static_cast<std::uint16_t>(source) < static_cast<std::uint16_t>(other.source);
}

bool topology_manager::rendezvous_key::operator<(const rendezvous_key& other) const noexcept {
   if (peer != other.peer) {
      return peer < other.peer;
   }
   return namespace_name < other.namespace_name;
}

bool topology_manager::stopping() const noexcept {
   const auto lock = std::scoped_lock{mutex_};
   return phase_ == phase::stopping || phase_ == phase::stopped;
}

std::chrono::steady_clock::time_point topology_manager::next_autonomous_wakeup() const {
   const auto steady_now = clocks_.steady_now();
   const auto system_now = clocks_.system_now();
   auto deadline = steady_now + policy_.refresh_interval;
   const auto saturating_deadline = [steady_now](std::chrono::system_clock::duration remaining) {
      if (remaining <= std::chrono::system_clock::duration::zero()) {
         return steady_now;
      }
      const auto available = std::chrono::steady_clock::time_point::max() - steady_now;
      if (remaining >= std::chrono::duration_cast<std::chrono::system_clock::duration>(available)) {
         return std::chrono::steady_clock::time_point::max();
      }
      return steady_now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(remaining);
   };

   const auto lock = std::scoped_lock{mutex_};
   for (const auto& [_, state] : rendezvous_clients_) {
      if (state.retry_after != std::chrono::steady_clock::time_point{}) {
         deadline = std::min(deadline, state.retry_after);
      }
      if (state.confirmed_registration && state.renew_after != std::chrono::system_clock::time_point{}) {
         deadline = std::min(deadline, saturating_deadline(state.renew_after - system_now));
      }
   }
   return deadline;
}

topology_manager::status topology_manager::current() const {
   const auto lock = std::scoped_lock{mutex_};
   auto waiting = std::size_t{};
   for (const auto& [_, count] : waiters_) {
      waiting += count;
   }
   return status{
       .lifecycle_phase = phase_,
       .refresh_queued = refresh_queued_,
       .refresh_in_flight = refresh_running_,
       .observations = observations_.size(),
       .active_operations = active_cancellations_.size(),
       .waiting_refreshes = waiting,
       .completed_refreshes = completed_refreshes_,
       .failed_refreshes = failed_refreshes_,
   };
}

std::uint64_t topology_manager::queue_refresh_locked() {
   if (refresh_running_) {
      return running_generation_;
   }
   if (refresh_queued_) {
      return queued_generation_;
   }
   refresh_queued_ = true;
   queued_generation_ = next_generation_++;
   return queued_generation_;
}

void topology_manager::release_waiter(std::uint64_t generation) noexcept {
   try {
      const auto lock = std::scoped_lock{mutex_};
      const auto waiter = waiters_.find(generation);
      if (waiter == waiters_.end() || waiter->second == 0) {
         return;
      }
      if (--waiter->second == 0) {
         waiters_.erase(waiter);
         completions_.erase(generation);
      }
   } catch (...) {
      // Waiter cancellation must not prevent the caller from receiving its original error.
   }
}

void topology_manager::start(lifecycle_tracker& lifecycle) {
   if (policy_.operating_mode == topology::mode::static_only) {
      const auto lock = std::scoped_lock{mutex_};
      if (phase_ == phase::stopping || phase_ == phase::stopped) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "cannot start P2P topology manager after shutdown");
      }
      if (started_) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P topology manager is already running");
      }
      // Static mode owns no lifecycle operation but remains observable as idle until shutdown is requested.
      started_ = true;
      parent_finished_ = true;
      return;
   }

   auto operation = lifecycle.track();
   if (!operation.active()) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "cannot start P2P topology manager after shutdown");
   }
   {
      const auto lock = std::scoped_lock{mutex_};
      if (phase_ == phase::stopping || phase_ == phase::stopped) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "cannot start P2P topology manager after shutdown");
      }
      if (started_) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P topology manager is already running");
      }
      started_ = true;
      phase_ = phase::running;
      static_cast<void>(queue_refresh_locked());
   }

   const auto executor = operation.executor();
   auto self = shared_from_this();
   try {
      boost::asio::co_spawn(
          executor, [self] { return self->async_run(); },
          [self = std::move(self), operation = std::move(operation)](std::exception_ptr error) mutable {
             self->finish_parent(error);
             operation.release();
          });
   } catch (...) {
      {
         const auto lock = std::scoped_lock{mutex_};
         phase_ = phase::idle;
         started_ = false;
         refresh_queued_ = false;
         queued_generation_ = 0;
      }
      operation.release();
      throw;
   }
   changed_->notify();
}

void topology_manager::request_stop() noexcept {
   auto cancellations = std::vector<std::shared_ptr<cancellation_latch>>{};
   try {
      {
         const auto lock = std::scoped_lock{mutex_};
         if (phase_ == phase::stopping || phase_ == phase::stopped) {
            return;
         }
         phase_ = phase::stopping;
         refresh_queued_ = false;
         queued_generation_ = 0;
         cancellations = active_cancellations_;
         if (policy_.operating_mode == topology::mode::static_only) {
            parent_finished_ = true;
            phase_ = phase::stopped;
         }
      }
      for (const auto& cancellation : cancellations) {
         cancellation->request_stop();
      }
      changed_->notify();
   } catch (...) {
      // Shutdown must continue even when an ancillary notification fails.
   }
}

void topology_manager::finish_parent(std::exception_ptr failure) noexcept {
   try {
      auto should_log = false;
      {
         const auto lock = std::scoped_lock{mutex_};
         should_log = failure && phase_ != phase::stopping && phase_ != phase::stopped;
         if (refresh_running_ && waiters_.contains(running_generation_)) {
            completions_.insert_or_assign(running_generation_, completion{.failure = failure});
         }
         refresh_running_ = false;
         refresh_queued_ = false;
         parent_finished_ = true;
         phase_ = phase::stopped;
      }
      if (should_log) {
         forge::exceptions::capture_and_log("P2P topology manager stopped unexpectedly");
      }
      changed_->notify();
   } catch (...) {
      // Completion handlers must remain noexcept so lifecycle tracking is released.
   }
}

void topology_manager::finish_refresh(std::uint64_t generation, std::vector<discovery::result> results,
                                      std::exception_ptr failure) noexcept {
   try {
      {
         const auto lock = std::scoped_lock{mutex_};
         refresh_running_ = false;
         running_generation_ = 0;
         ++completed_refreshes_;
         if (failure) {
            ++failed_refreshes_;
         }
         if (waiters_.contains(generation)) {
            completions_.insert_or_assign(generation,
                                          completion{.results = std::move(results), .failure = std::move(failure)});
         }
      }
      changed_->notify();
   } catch (...) {
      // Refresh completion must not strand the lifecycle parent operation.
   }
}

void topology_manager::add_cancellation(const std::shared_ptr<cancellation_latch>& cancellation) {
   auto cancel_now = false;
   {
      const auto lock = std::scoped_lock{mutex_};
      active_cancellations_.push_back(cancellation);
      cancel_now = phase_ == phase::stopping || phase_ == phase::stopped;
   }
   if (cancel_now) {
      cancellation->request_stop();
   }
}

void topology_manager::remove_cancellation(const std::shared_ptr<cancellation_latch>& cancellation) noexcept {
   try {
      const auto lock = std::scoped_lock{mutex_};
      std::erase(active_cancellations_, cancellation);
   } catch (...) {
   }
}

boost::asio::awaitable<std::vector<discovery::result>> topology_manager::async_refresh() {
   if (policy_.operating_mode == topology::mode::static_only) {
      co_return std::vector<discovery::result>{};
   }

   auto generation = std::uint64_t{};
   {
      const auto lock = std::scoped_lock{mutex_};
      if (phase_ == phase::stopping || phase_ == phase::stopped) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "P2P topology manager is stopped");
      }
      if (!started_) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "P2P topology refresh requires a started node");
      }
      generation = queue_refresh_locked();
      ++waiters_[generation];
   }
   changed_->notify();

   while (true) {
      const auto observed = changed_->epoch();
      auto result = std::optional<completion>{};
      auto stopped = false;
      {
         const auto lock = std::scoped_lock{mutex_};
         if (const auto completed = completions_.find(generation); completed != completions_.end()) {
            result = completed->second;
         } else {
            stopped = phase_ == phase::stopping || phase_ == phase::stopped;
         }
      }
      if (result) {
         release_waiter(generation);
         if (result->failure) {
            std::rethrow_exception(result->failure);
         }
         co_return std::move(result->results);
      }
      if (stopped) {
         release_waiter(generation);
         FORGE_THROW_EXCEPTION(exceptions::closed, "P2P topology manager stopped during refresh");
      }
      try {
         static_cast<void>(co_await changed_->async_wait(observed));
      } catch (...) {
         release_waiter(generation);
         throw;
      }
   }
}

boost::asio::awaitable<void> topology_manager::async_join() {
   while (true) {
      const auto observed = changed_->epoch();
      {
         const auto lock = std::scoped_lock{mutex_};
         if (!started_ || parent_finished_ || policy_.operating_mode == topology::mode::static_only) {
            co_return;
         }
      }
      static_cast<void>(co_await changed_->async_wait(observed));
   }
}

boost::asio::awaitable<void> topology_manager::async_run() {
   try {
      while (true) {
         const auto observed = changed_->epoch();
         auto generation = std::uint64_t{};
         {
            const auto lock = std::scoped_lock{mutex_};
            if (phase_ != phase::running) {
               break;
            }
            if (refresh_queued_) {
               generation = queued_generation_;
               refresh_queued_ = false;
               queued_generation_ = 0;
               refresh_running_ = true;
               running_generation_ = generation;
            }
         }

         if (generation != 0) {
            co_await async_refresh_generation(generation);
            continue;
         }

         const auto deadline = next_autonomous_wakeup();
         if (clocks_.before_idle_wait) {
            clocks_.before_idle_wait();
         }
         try {
            if (clocks_.idle_wait) {
               co_await clocks_.idle_wait(deadline);
            } else {
               static_cast<void>(co_await changed_->async_wait_until(observed, deadline));
            }
         } catch (...) {
            if (!stopping()) {
               forge::exceptions::capture_and_log("P2P topology manager timer failed");
            }
         }
         {
            const auto lock = std::scoped_lock{mutex_};
            if (phase_ == phase::running && !refresh_queued_ && !refresh_running_ &&
                clocks_.steady_now() >= deadline) {
               static_cast<void>(queue_refresh_locked());
            }
         }
      }
   } catch (...) {
      if (!stopping()) {
         forge::exceptions::capture_and_log("P2P topology manager failed");
      }
   }
   try {
      co_await async_unregister_rendezvous();
   } catch (...) {
      forge::exceptions::capture_and_log("P2P topology rendezvous unregister failed");
   }
}

boost::asio::awaitable<void> topology_manager::async_refresh_generation(std::uint64_t generation) {
   auto results = std::vector<discovery::result>{};
   auto failure = std::exception_ptr{};
   try {
      results = co_await async_collect_discovery();
      merge_observations(results);
      co_await async_reconcile_sessions();
   } catch (...) {
      failure = std::current_exception();
      if (!stopping()) {
         forge::exceptions::capture_and_log("P2P topology refresh failed");
      }
   }
   finish_refresh(generation, std::move(results), std::move(failure));
}

} // namespace forge::net::p2p::detail
