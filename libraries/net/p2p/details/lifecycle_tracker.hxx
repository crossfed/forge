#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/strand.hpp>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

namespace forge::net::p2p::detail {

class lifecycle_wakeup;

class lifecycle_tracker {
 private:
   struct state {
      struct operation_context {
         explicit operation_context(boost::asio::any_io_executor executor);

         boost::asio::strand<boost::asio::any_io_executor> strand;
      };

      explicit state(boost::asio::any_io_executor executor_value);
      void release(std::uint64_t id) noexcept;

      boost::asio::any_io_executor executor;
      mutable std::mutex mutex;
      lifecycle_phase phase = lifecycle_phase::idle;
      bool stop_requested = false;
      std::uint64_t next_operation_id = 1;
      std::map<std::uint64_t, std::shared_ptr<operation_context>> operations;
      std::shared_ptr<std::atomic_bool> stop_latch = std::make_shared<std::atomic_bool>(false);
      std::shared_ptr<lifecycle_wakeup> changed;
   };

 public:
   class operation {
    public:
      operation() = default;
      operation(const operation&) = delete;
      operation& operator=(const operation&) = delete;
      operation(operation&& other) noexcept;
      operation& operator=(operation&& other) noexcept;
      ~operation();

      [[nodiscard]] bool active() const noexcept;
      [[nodiscard]] boost::asio::any_io_executor executor() const noexcept;
      [[nodiscard]] std::shared_ptr<const std::atomic_bool> stop_latch() const noexcept;
      void release() noexcept;

    private:
      operation(std::shared_ptr<state> state, std::uint64_t id, std::shared_ptr<state::operation_context> context);

      std::shared_ptr<state> state_;
      std::uint64_t id_ = 0;
      std::shared_ptr<state::operation_context> context_;

      friend class lifecycle_tracker;
   };

   explicit lifecycle_tracker(boost::asio::any_io_executor executor);

   [[nodiscard]] bool begin_start() noexcept;
   void set_phase(lifecycle_phase value) noexcept;
   [[nodiscard]] lifecycle_phase phase() const noexcept;
   [[nodiscard]] bool stop_requested() const noexcept;
   [[nodiscard]] operation track() noexcept;
   void request_stop() noexcept;
   boost::asio::awaitable<void> wait() const;
   void finish_stop() noexcept;

 private:
   std::shared_ptr<state> state_;
};

} // namespace forge::net::p2p::detail
