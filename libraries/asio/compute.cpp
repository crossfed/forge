module;

#include "details/stop_state.hxx"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

module forge.asio.compute;

import forge.asio.exceptions;

namespace forge::asio::compute {
namespace {

enum class admission_state : std::uint8_t {
   queued,
   granted,
   canceled,
   rejected,
   completed,
};

struct async_waiter {
   explicit async_waiter(boost::asio::any_io_executor executor)
       : strand{boost::asio::make_strand(std::move(executor))},
         timer{strand, boost::asio::steady_timer::time_point::max()} {}

   boost::asio::strand<boost::asio::any_io_executor> strand;
   boost::asio::steady_timer timer;
};

struct admission_waiter : async_waiter {
   admission_waiter(boost::asio::any_io_executor executor, std::uint64_t waiter_id)
       : async_waiter{std::move(executor)}, id{waiter_id} {}

   std::uint64_t id = 0;
   admission_state state = admission_state::queued;
   forge::asio::detail::stop_state stop_state;
};

void wake(const std::shared_ptr<async_waiter>& waiter) noexcept {
   boost::asio::dispatch(waiter->strand, [waiter] {
      try {
         waiter->timer.expires_at(boost::asio::steady_timer::time_point::min());
         waiter->timer.cancel();
      } catch (...) {
         // Completion and shutdown paths must stay noexcept.
      }
   });
}

std::exception_ptr canceled_error(std::string message = "compute operation was canceled") {
   return std::make_exception_ptr(exceptions::canceled{std::move(message)});
}

void set_current_thread_name(const std::string& name) noexcept {
   if (name.empty()) {
      return;
   }
#if defined(__APPLE__)
   static_cast<void>(pthread_setname_np(name.c_str()));
#elif defined(__linux__)
   auto limited = name.substr(0, 15);
   static_cast<void>(pthread_setname_np(pthread_self(), limited.c_str()));
#else
   static_cast<void>(name);
#endif
}

thread_local std::optional<std::size_t> current_worker_index;

} // namespace

context::context(std::stop_token stop_token) noexcept : stop_token_{std::move(stop_token)} {}

std::stop_token context::stop_token() const noexcept {
   return stop_token_;
}

bool context::stop_requested() const noexcept {
   return stop_token_.stop_requested();
}

void context::throw_if_stop_requested() const {
   if (stop_requested()) {
      throw exceptions::canceled{"compute operation was canceled"};
   }
}

namespace detail {

struct operation_state::impl {
   impl(std::shared_ptr<pool_state> owner_value, std::uint64_t operation_id)
       : owner{std::move(owner_value)}, id{operation_id} {}

   std::weak_ptr<pool_state> owner;
   std::uint64_t id = 0;
   forge::asio::detail::stop_state stop;
   mutable std::mutex mutex;
   bool completed = false;
   bool wait_started = false;
   std::exception_ptr error;
   std::shared_ptr<async_waiter> waiter;
};

operation_state::operation_state(std::shared_ptr<pool_state> owner, std::uint64_t id)
    : impl_{std::make_unique<impl>(std::move(owner), id)} {}

operation_state::~operation_state() = default;

std::uint64_t operation_state::id() const noexcept {
   return impl_->id;
}

bool operation_state::cancel() noexcept {
   const auto changed = impl_->stop.request_stop();
   if (changed) {
      if (auto owner = impl_->owner.lock()) {
         static_cast<void>(owner->cancel(impl_->id));
      }
   }
   return changed;
}

bool operation_state::stop_requested() const noexcept {
   return impl_->stop.stop_requested();
}

std::stop_token operation_state::stop_token() const noexcept {
   return impl_->stop.token();
}

void operation_state::link_parent(std::stop_token parent) {
   auto weak = weak_from_this();
   impl_->stop.link(std::move(parent), [weak] {
      if (auto self = weak.lock()) {
         static_cast<void>(self->cancel());
      }
   });
}

void operation_state::request_stop_only() noexcept {
   static_cast<void>(impl_->stop.request_stop());
}

bool operation_state::complete_value() noexcept {
   auto waiter = std::shared_ptr<async_waiter>{};
   {
      const auto lock = std::scoped_lock{impl_->mutex};
      if (impl_->completed) {
         return false;
      }
      impl_->completed = true;
      waiter = impl_->waiter;
   }
   if (waiter != nullptr) {
      wake(waiter);
   }
   return true;
}

bool operation_state::complete_exception(std::exception_ptr error) noexcept {
   auto waiter = std::shared_ptr<async_waiter>{};
   {
      const auto lock = std::scoped_lock{impl_->mutex};
      if (impl_->completed) {
         return false;
      }
      impl_->error = std::move(error);
      impl_->completed = true;
      waiter = impl_->waiter;
   }
   if (waiter != nullptr) {
      wake(waiter);
   }
   return true;
}

boost::asio::awaitable<void> operation_state::wait() {
   const auto executor = co_await boost::asio::this_coro::executor;
   auto cancellation = co_await boost::asio::this_coro::cancellation_state;
   auto waiter = std::make_shared<async_waiter>(executor);
   auto self = shared_from_this();

   {
      const auto lock = std::scoped_lock{impl_->mutex};
      if (impl_->wait_started) {
         throw exceptions::invalid_state{"compute operation can only be awaited once"};
      }
      impl_->wait_started = true;
      if (impl_->completed) {
         if (impl_->error) {
            std::rethrow_exception(impl_->error);
         }
         co_return;
      }
      impl_->waiter = waiter;
   }

   auto slot = cancellation.slot();
   if (slot.is_connected()) {
      slot.assign([weak = std::weak_ptr<operation_state>{self}](boost::asio::cancellation_type_t type) {
         if (type != boost::asio::cancellation_type::none) {
            if (auto locked = weak.lock()) {
               static_cast<void>(locked->cancel());
            }
         }
      });
   }
   if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
      static_cast<void>(cancel());
      if (slot.is_connected()) {
         slot.clear();
      }
      throw exceptions::canceled{"compute operation wait was canceled"};
   }

   auto switch_error = boost::system::error_code{};
   co_await boost::asio::dispatch(waiter->strand,
                                  boost::asio::redirect_error(boost::asio::use_awaitable, switch_error));
   if (switch_error) {
      static_cast<void>(cancel());
      if (slot.is_connected()) {
         slot.clear();
      }
      throw exceptions::internal{"failed to arm compute operation wait"};
   }

   auto wait_error = boost::system::error_code{};
   co_await waiter->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, wait_error));
   static_cast<void>(wait_error);

   const auto wait_canceled = cancellation.cancelled() != boost::asio::cancellation_type::none;
   if (slot.is_connected()) {
      slot.clear();
   }
   if (wait_canceled) {
      static_cast<void>(cancel());
      throw exceptions::canceled{"compute operation wait was canceled"};
   }

   auto error = std::exception_ptr{};
   auto completed = false;
   {
      const auto lock = std::scoped_lock{impl_->mutex};
      completed = impl_->completed;
      error = impl_->error;
      impl_->waiter.reset();
   }
   if (!completed) {
      static_cast<void>(cancel());
      throw exceptions::internal{"compute operation wake did not carry a completion"};
   }
   if (error) {
      std::rethrow_exception(error);
   }
}

reservation::reservation(std::shared_ptr<pool_state> owner, std::uint64_t id) noexcept
    : owner_{std::move(owner)}, id_{id} {}

reservation::~reservation() {
   if (owner_ != nullptr && id_ != 0) {
      owner_->release_reservation(id_);
   }
}

reservation::reservation(reservation&& other) noexcept
    : owner_{std::move(other.owner_)}, id_{std::exchange(other.id_, 0)} {}

reservation& reservation::operator=(reservation&& other) noexcept {
   if (this != &other) {
      if (owner_ != nullptr && id_ != 0) {
         owner_->release_reservation(id_);
      }
      owner_ = std::move(other.owner_);
      id_ = std::exchange(other.id_, 0);
   }
   return *this;
}

std::uint64_t reservation::id() const noexcept {
   return id_;
}

void reservation::consume() noexcept {
   id_ = 0;
   owner_.reset();
}

struct pool_state::impl {
   struct job {
      std::uint64_t id = 0;
      std::string name;
      std::shared_ptr<operation_state> operation;
      std::function<void(context&)> work;
      std::chrono::steady_clock::time_point queued_at;
   };

   explicit impl(configuration input)
       : config{std::move(input)},
         worker_threads{config.worker_threads == 0 ? std::max<std::size_t>(1, std::thread::hardware_concurrency())
                                                   : config.worker_threads},
         capacity{worker_threads + config.max_pending_tasks},
         native_pool{std::make_unique<boost::asio::thread_pool>(worker_threads)} {
      if (capacity < worker_threads) {
         throw exceptions::invalid_options{"compute pool capacity overflow"};
      }
      initialize_workers();
   }

   ~impl() = default;

   void initialize_workers() {
      auto ready = std::latch{static_cast<std::ptrdiff_t>(worker_threads)};
      auto release = std::latch{1};
      auto exited = std::latch{static_cast<std::ptrdiff_t>(worker_threads)};
      auto error_mutex = std::mutex{};
      auto error = std::exception_ptr{};

      for (std::size_t index = 0; index < worker_threads; ++index) {
         boost::asio::post(native_pool->get_executor(), [&, index] {
            current_worker_index = index;
            set_current_thread_name(config.thread_name);
            try {
               if (config.on_worker_start) {
                  config.on_worker_start(index);
               }
            } catch (...) {
               const auto lock = std::scoped_lock{error_mutex};
               if (!error) {
                  error = std::current_exception();
               }
            }
            ready.count_down();
            release.wait();
            exited.count_down();
         });
      }

      ready.wait();
      release.count_down();
      exited.wait();
      if (error) {
         native_pool->stop();
         native_pool->join();
         throw exceptions::internal{"compute worker start hook failed"};
      }
   }

   [[nodiscard]] std::size_t occupied_locked() const noexcept {
      return reservations.size() + pending.size() + running_operations.size();
   }

   [[nodiscard]] bool drained_locked() const noexcept {
      return pending.empty() && running_operations.empty();
   }

   std::vector<std::shared_ptr<admission_waiter>> grant_waiters_locked() {
      auto granted = std::vector<std::shared_ptr<admission_waiter>>{};
      while (!stopping && occupied_locked() < capacity && !admission_waiters.empty()) {
         auto waiter = std::move(admission_waiters.front());
         admission_waiters.pop_front();
         if (waiter->state != admission_state::queued) {
            continue;
         }
         waiter->state = admission_state::granted;
         reservations.insert(waiter->id);
         granted.push_back(std::move(waiter));
      }
      current_metrics.waiting = admission_waiters.size();
      return granted;
   }

   std::vector<job> start_jobs_locked() {
      auto jobs = std::vector<job>{};
      while (!stopping && running_operations.size() < worker_threads && !pending.empty()) {
         auto next = std::move(pending.front());
         pending.pop_front();
         running_operations.emplace(next.id, next.operation);
         jobs.push_back(std::move(next));
      }
      current_metrics.pending = pending.size();
      current_metrics.running = running_operations.size();
      return jobs;
   }

   void post_jobs(const std::shared_ptr<pool_state>& owner, std::vector<job> jobs) {
      for (auto& next : jobs) {
         boost::asio::post(native_pool->get_executor(), [owner, next = std::move(next)]() mutable {
            owner->impl_->run_job(owner, std::move(next));
         });
      }
   }

   void run_job(const std::shared_ptr<pool_state>& owner, job current) noexcept {
      const auto started_at = std::chrono::steady_clock::now();
      auto failed = false;
      auto canceled = false;
      auto error = std::exception_ptr{};

      try {
         auto work_context = context{current.operation->stop_token()};
         work_context.throw_if_stop_requested();
         current.work(work_context);
         if (current.operation->stop_requested()) {
            throw exceptions::canceled{"compute operation was canceled"};
         }
      } catch (const exceptions::canceled&) {
         canceled = true;
         error = std::current_exception();
      } catch (...) {
         failed = true;
         error = std::current_exception();
      }

      if (error) {
         current.operation->complete_exception(std::move(error));
      } else {
         current.operation->complete_value();
      }

      auto jobs = std::vector<job>{};
      auto granted = std::vector<std::shared_ptr<admission_waiter>>{};
      auto drain = std::vector<std::shared_ptr<async_waiter>>{};
      {
         const auto lock = std::scoped_lock{mutex};
         running_operations.erase(current.id);
         if (canceled) {
            ++current_metrics.canceled;
         } else if (failed) {
            ++current_metrics.failed;
         } else {
            ++current_metrics.completed;
         }
         current_metrics.total_queue_time +=
             std::chrono::duration_cast<std::chrono::nanoseconds>(started_at - current.queued_at);
         current_metrics.total_execution_time +=
             std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started_at);
         jobs = start_jobs_locked();
         granted = grant_waiters_locked();
         if (drained_locked()) {
            drain.swap(drain_waiters);
         }
      }

      post_jobs(owner, std::move(jobs));
      for (const auto& waiter : granted) {
         wake(waiter);
      }
      for (const auto& waiter : drain) {
         wake(waiter);
      }
      drained_cv.notify_all();
   }

   void cancel_waiter(const std::shared_ptr<admission_waiter>& waiter, bool rejected) noexcept {
      auto should_wake = false;
      {
         const auto lock = std::scoped_lock{mutex};
         if (waiter->state == admission_state::queued) {
            waiter->state = rejected ? admission_state::rejected : admission_state::canceled;
            const auto found = std::ranges::find(admission_waiters, waiter);
            if (found != admission_waiters.end()) {
               admission_waiters.erase(found);
            }
            if (rejected) {
               ++current_metrics.rejected;
            } else {
               ++current_metrics.canceled;
            }
            current_metrics.waiting = admission_waiters.size();
            should_wake = true;
         } else if (waiter->state == admission_state::granted) {
            waiter->state = rejected ? admission_state::rejected : admission_state::canceled;
            reservations.erase(waiter->id);
            if (rejected) {
               ++current_metrics.rejected;
            } else {
               ++current_metrics.canceled;
            }
            should_wake = true;
         }
      }
      if (should_wake) {
         wake(waiter);
      }
   }

   boost::asio::awaitable<void> wait_for_drain() {
      const auto executor = co_await boost::asio::this_coro::executor;
      auto waiter = std::make_shared<async_waiter>(executor);
      {
         const auto lock = std::scoped_lock{mutex};
         if (drained_locked()) {
            co_return;
         }
         drain_waiters.push_back(waiter);
      }
      auto error = boost::system::error_code{};
      co_await waiter->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      static_cast<void>(error);
   }

   void finalize() {
      const auto lock = std::scoped_lock{finalize_mutex};
      if (finalized) {
         if (finalize_error) {
            std::rethrow_exception(finalize_error);
         }
         return;
      }

      if (config.on_worker_stop) {
         auto ready = std::latch{static_cast<std::ptrdiff_t>(worker_threads)};
         auto release = std::latch{1};
         auto exited = std::latch{static_cast<std::ptrdiff_t>(worker_threads)};
         auto error_mutex = std::mutex{};
         for (std::size_t index = 0; index < worker_threads; ++index) {
            boost::asio::post(native_pool->get_executor(), [&] {
               try {
                  if (!current_worker_index.has_value()) {
                     throw exceptions::internal{"compute worker identity is unavailable"};
                  }
                  config.on_worker_stop(*current_worker_index);
               } catch (...) {
                  const auto error_lock = std::scoped_lock{error_mutex};
                  if (!finalize_error) {
                     finalize_error = std::current_exception();
                  }
               }
               ready.count_down();
               release.wait();
               current_worker_index.reset();
               exited.count_down();
            });
         }
         ready.wait();
         release.count_down();
         exited.wait();
      }

      native_pool->join();
      finalized = true;
      if (finalize_error) {
         throw exceptions::internal{"compute worker stop hook failed"};
      }
   }

   configuration config;
   std::size_t worker_threads = 0;
   std::size_t capacity = 0;
   std::unique_ptr<boost::asio::thread_pool> native_pool;
   mutable std::mutex mutex;
   std::condition_variable drained_cv;
   std::deque<std::shared_ptr<admission_waiter>> admission_waiters;
   std::unordered_set<std::uint64_t> reservations;
   std::deque<job> pending;
   std::unordered_map<std::uint64_t, std::shared_ptr<operation_state>> running_operations;
   std::vector<std::shared_ptr<async_waiter>> drain_waiters;
   std::atomic_uint64_t next_id = 1;
   bool stopping = false;
   mutable metrics_snapshot current_metrics{};
   std::mutex finalize_mutex;
   std::exception_ptr finalize_error;
   bool finalized = false;
};

pool_state::pool_state(configuration configuration) : impl_{std::make_unique<impl>(std::move(configuration))} {}

pool_state::~pool_state() = default;

boost::asio::awaitable<reservation> pool_state::reserve(std::stop_token parent) {
   if (parent.stop_requested()) {
      throw exceptions::canceled{"compute submission was canceled"};
   }

   const auto executor = co_await boost::asio::this_coro::executor;
   auto cancellation = co_await boost::asio::this_coro::cancellation_state;
   auto waiter = std::shared_ptr<admission_waiter>{};
   {
      const auto lock = std::scoped_lock{impl_->mutex};
      if (impl_->stopping) {
         ++impl_->current_metrics.rejected;
         throw exceptions::rejected{"compute pool is stopped"};
      }
      if (impl_->admission_waiters.empty() && impl_->occupied_locked() < impl_->capacity) {
         const auto id = impl_->next_id.fetch_add(1, std::memory_order_relaxed);
         impl_->reservations.insert(id);
         co_return reservation{shared_from_this(), id};
      }
      if (impl_->admission_waiters.size() >= impl_->config.max_waiting_submissions) {
         ++impl_->current_metrics.rejected;
         throw exceptions::rejected{"compute submission wait queue is full"};
      }

      const auto id = impl_->next_id.fetch_add(1, std::memory_order_relaxed);
      waiter = std::make_shared<admission_waiter>(executor, id);
      impl_->admission_waiters.push_back(waiter);
      impl_->current_metrics.waiting = impl_->admission_waiters.size();
   }

   auto weak_owner = weak_from_this();
   waiter->stop_state.link(std::move(parent), [weak_owner, weak_waiter = std::weak_ptr{waiter}] {
      if (auto owner = weak_owner.lock()) {
         if (auto locked_waiter = weak_waiter.lock()) {
            owner->impl_->cancel_waiter(locked_waiter, false);
         }
      }
   });

   auto slot = cancellation.slot();
   if (slot.is_connected()) {
      slot.assign([weak_owner, weak_waiter = std::weak_ptr{waiter}](boost::asio::cancellation_type_t type) {
         if (type != boost::asio::cancellation_type::none) {
            if (auto owner = weak_owner.lock()) {
               if (auto locked_waiter = weak_waiter.lock()) {
                  owner->impl_->cancel_waiter(locked_waiter, false);
               }
            }
         }
      });
   }
   if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
      impl_->cancel_waiter(waiter, false);
   }

   auto switch_error = boost::system::error_code{};
   co_await boost::asio::dispatch(waiter->strand,
                                  boost::asio::redirect_error(boost::asio::use_awaitable, switch_error));
   if (!switch_error && cancellation.cancelled() == boost::asio::cancellation_type::none) {
      auto wait_error = boost::system::error_code{};
      co_await waiter->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, wait_error));
      static_cast<void>(wait_error);
   }

   const auto canceled_after_wait = cancellation.cancelled() != boost::asio::cancellation_type::none;
   if (slot.is_connected()) {
      slot.clear();
   }
   if (canceled_after_wait) {
      impl_->cancel_waiter(waiter, false);
   }

   {
      const auto lock = std::scoped_lock{impl_->mutex};
      if (waiter->state == admission_state::granted) {
         waiter->state = admission_state::completed;
         co_return reservation{shared_from_this(), waiter->id};
      }
      if (waiter->state == admission_state::rejected) {
         throw exceptions::rejected{"compute pool stopped while submission was waiting"};
      }
   }
   throw exceptions::canceled{"compute submission was canceled"};
}

std::optional<reservation> pool_state::try_reserve(std::stop_token parent) {
   if (parent.stop_requested()) {
      throw exceptions::canceled{"compute submission was canceled"};
   }
   const auto lock = std::scoped_lock{impl_->mutex};
   if (impl_->stopping) {
      ++impl_->current_metrics.rejected;
      throw exceptions::rejected{"compute pool is stopped"};
   }
   if (!impl_->admission_waiters.empty() || impl_->occupied_locked() >= impl_->capacity) {
      ++impl_->current_metrics.rejected;
      return std::nullopt;
   }
   const auto id = impl_->next_id.fetch_add(1, std::memory_order_relaxed);
   impl_->reservations.insert(id);
   return reservation{shared_from_this(), id};
}

void pool_state::release_reservation(std::uint64_t id) noexcept {
   auto granted = std::vector<std::shared_ptr<admission_waiter>>{};
   {
      const auto lock = std::scoped_lock{impl_->mutex};
      if (impl_->reservations.erase(id) == 0) {
         return;
      }
      granted = impl_->grant_waiters_locked();
   }
   for (const auto& waiter : granted) {
      wake(waiter);
   }
}

void pool_state::submit(reservation reservation_value, std::string name, std::shared_ptr<operation_state> operation,
                        std::function<void(context&)> work) {
   const auto id = reservation_value.id();
   auto jobs = std::vector<impl::job>{};
   auto reject = false;
   auto cancel_before_start = false;
   {
      const auto lock = std::scoped_lock{impl_->mutex};
      if (impl_->reservations.erase(id) == 0) {
         throw exceptions::internal{"compute admission reservation is invalid"};
      }
      reservation_value.consume();
      if (impl_->stopping) {
         ++impl_->current_metrics.rejected;
         reject = true;
      } else {
         ++impl_->current_metrics.submitted;
         if (operation->stop_requested()) {
            ++impl_->current_metrics.canceled;
            cancel_before_start = true;
         } else {
            impl_->pending.push_back(impl::job{
                .id = id,
                .name = std::move(name),
                .operation = operation,
                .work = std::move(work),
                .queued_at = std::chrono::steady_clock::now(),
            });
            jobs = impl_->start_jobs_locked();
         }
      }
   }

   if (reject) {
      throw exceptions::rejected{"compute pool is stopped"};
   }
   if (cancel_before_start) {
      operation->complete_exception(canceled_error());
      return;
   }
   impl_->post_jobs(shared_from_this(), std::move(jobs));
}

bool pool_state::cancel(std::uint64_t id) noexcept {
   auto operation = std::shared_ptr<operation_state>{};
   auto granted = std::vector<std::shared_ptr<admission_waiter>>{};
   auto found_running = false;
   {
      const auto lock = std::scoped_lock{impl_->mutex};
      const auto found = std::ranges::find_if(impl_->pending, [id](const impl::job& value) { return value.id == id; });
      if (found != impl_->pending.end()) {
         operation = found->operation;
         impl_->pending.erase(found);
         ++impl_->current_metrics.canceled;
         impl_->current_metrics.pending = impl_->pending.size();
         granted = impl_->grant_waiters_locked();
      } else {
         found_running = impl_->running_operations.contains(id);
      }
   }
   if (operation != nullptr) {
      operation->complete_exception(canceled_error());
   }
   for (const auto& waiter : granted) {
      wake(waiter);
   }
   return operation != nullptr || found_running;
}

void pool_state::request_stop() noexcept {
   auto waiting = std::vector<std::shared_ptr<admission_waiter>>{};
   auto pending = std::vector<std::shared_ptr<operation_state>>{};
   auto running = std::vector<std::shared_ptr<operation_state>>{};
   auto drain = std::vector<std::shared_ptr<async_waiter>>{};
   {
      const auto lock = std::scoped_lock{impl_->mutex};
      if (impl_->stopping) {
         return;
      }
      impl_->stopping = true;
      impl_->current_metrics.stopped = true;

      for (auto& waiter : impl_->admission_waiters) {
         waiter->state = admission_state::rejected;
         waiting.push_back(waiter);
      }
      impl_->current_metrics.rejected += waiting.size();
      impl_->admission_waiters.clear();
      impl_->current_metrics.waiting = 0;

      for (auto& value : impl_->pending) {
         pending.push_back(value.operation);
      }
      impl_->current_metrics.canceled += pending.size();
      impl_->pending.clear();
      impl_->current_metrics.pending = 0;

      for (const auto& [id, operation] : impl_->running_operations) {
         static_cast<void>(id);
         running.push_back(operation);
      }

      if (impl_->drained_locked()) {
         drain.swap(impl_->drain_waiters);
      }
   }

   for (const auto& waiter : waiting) {
      wake(waiter);
   }
   for (const auto& operation : pending) {
      operation->request_stop_only();
      operation->complete_exception(canceled_error("compute pool stopped before operation started"));
   }
   for (const auto& operation : running) {
      operation->request_stop_only();
   }
   for (const auto& waiter : drain) {
      wake(waiter);
   }

   // Running operations own themselves through the native jobs. Requesting stop
   // through their operation handles is handled by shutdown below.
   impl_->drained_cv.notify_all();
}

boost::asio::awaitable<void> pool_state::shutdown() {
   request_stop();
   co_await impl_->wait_for_drain();
   impl_->finalize();
}

void pool_state::shutdown_sync() noexcept {
   request_stop();
   {
      auto lock = std::unique_lock{impl_->mutex};
      impl_->drained_cv.wait(lock, [this] { return impl_->drained_locked(); });
   }
   try {
      impl_->finalize();
   } catch (...) {
      // Destructors cannot report hook failures.
   }
}

pool_state::metrics_snapshot pool_state::snapshot() const {
   const auto lock = std::scoped_lock{impl_->mutex};
   auto value = impl_->current_metrics;
   value.waiting = impl_->admission_waiters.size();
   value.pending = impl_->pending.size();
   value.running = impl_->running_operations.size();
   value.stopped = impl_->stopping;
   return value;
}

} // namespace detail

pool::pool() : pool(options{}) {}

pool::pool(options options_value)
    : state_{std::make_shared<detail::pool_state>(detail::pool_state::configuration{
          .worker_threads = options_value.worker_threads,
          .max_pending_tasks = options_value.max_pending_tasks,
          .max_waiting_submissions = options_value.max_waiting_submissions,
          .thread_name = std::move(options_value.thread_name),
          .on_worker_start = std::move(options_value.on_worker_start),
          .on_worker_stop = std::move(options_value.on_worker_stop),
      })} {}

pool::~pool() {
   if (state_ != nullptr) {
      state_->shutdown_sync();
   }
}

executor pool::get_executor() const noexcept {
   return executor{state_};
}

metrics pool::snapshot() const {
   return state_->snapshot();
}

void pool::request_stop() noexcept {
   state_->request_stop();
}

boost::asio::awaitable<void> pool::shutdown() {
   co_await state_->shutdown();
}

} // namespace forge::asio::compute
