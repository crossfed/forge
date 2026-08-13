#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "lifecycle_tracker.hxx"

namespace forge::net::p2p {

class cancellation_latch;

} // namespace forge::net::p2p

namespace forge::net::p2p::detail {

class lifecycle_wakeup;

class bootstrap_service : public std::enable_shared_from_this<bootstrap_service> {
 public:
   struct callbacks {
      std::function<boost::asio::awaitable<peer_id>(bootstrap_peer, std::chrono::milliseconds,
                                                    std::shared_ptr<cancellation_latch>)>
          connect;
      std::function<bool(const bootstrap_peer&, const peer_id&)> connected;
      std::function<void(const peer_id&)> protect;
      std::function<void(const peer_id&)> unprotect;
      std::function<boost::asio::awaitable<void>()> prune_peer_state;
   };

   bootstrap_service(boost::asio::any_io_executor executor, lifecycle_options options, callbacks callbacks_value);

   boost::asio::awaitable<std::size_t> async_initial_bootstrap();
   boost::asio::awaitable<void> async_set_bootstrap(std::vector<bootstrap_peer> peers);
   void start_maintenance(lifecycle_tracker& tracker);
   void request_stop() noexcept;
   [[nodiscard]] std::size_t configured_count() const noexcept;
   [[nodiscard]] std::size_t connected_count() const;
   [[nodiscard]] std::string last_failure() const;

 private:
   struct entry {
      bootstrap_peer configured;
      std::optional<peer_id> connected_peer;
      std::optional<peer_id> protected_peer;
      std::chrono::steady_clock::time_point next_attempt{};
      std::size_t failures = 0;
      std::uint64_t generation = 0;
      std::shared_ptr<cancellation_latch> active_cancellation;
   };

   struct batch_state {
      batch_state(boost::asio::strand<boost::asio::any_io_executor> executor, std::vector<std::string> keys_value,
                  std::optional<std::chrono::steady_clock::time_point> deadline_value, bool stop_after_connection_value,
                  std::size_t workers);

      std::vector<std::string> keys;
      std::optional<std::chrono::steady_clock::time_point> deadline;
      bool stop_after_connection = false;
      bool connected = false;
      std::size_t next = 0;
      std::size_t remaining_workers = 0;
      std::exception_ptr failure;
      boost::asio::steady_timer completion;
   };

   [[nodiscard]] static std::string key_for(const bootstrap_peer& peer);
   [[nodiscard]] std::chrono::milliseconds retry_delay(std::size_t failures) const;
   [[nodiscard]] bool stopping() const noexcept;
   [[nodiscard]] std::vector<std::string> all_keys() const;
   [[nodiscard]] std::vector<std::string> due_keys(std::chrono::steady_clock::time_point now) const;
   [[nodiscard]] std::chrono::milliseconds next_maintenance_delay(std::chrono::steady_clock::time_point now) const;
   boost::asio::awaitable<bool> async_attempt(const std::string& key, std::chrono::milliseconds timeout);
   boost::asio::awaitable<bool> async_run_batch(std::vector<std::string> keys,
                                                std::optional<std::chrono::steady_clock::time_point> deadline,
                                                bool stop_after_connection);
   boost::asio::awaitable<bool> async_run_batch_on_strand(std::vector<std::string> keys,
                                                          std::optional<std::chrono::steady_clock::time_point> deadline,
                                                          bool stop_after_connection);
   boost::asio::awaitable<void> async_batch_worker(std::shared_ptr<batch_state> batch);
   boost::asio::awaitable<void> async_wait_for_retry(std::chrono::steady_clock::time_point deadline);
   boost::asio::awaitable<void> async_maintenance();
   void replace_bootstrap(std::vector<bootstrap_peer> peers);
   void cancel_attempts(const std::vector<std::string>& keys,
                        std::optional<std::string> except = std::nullopt) noexcept;
   void wake_retry_wait() noexcept;

   boost::asio::any_io_executor executor_;
   boost::asio::strand<boost::asio::any_io_executor> strand_;
   lifecycle_options options_;
   callbacks callbacks_;
   mutable std::mutex mutex_;
   std::map<std::string, entry> entries_;
   std::shared_ptr<lifecycle_wakeup> retry_wakeup_;
   std::uint64_t next_generation_ = 1;
   std::string last_failure_;
   bool maintenance_started_ = false;
   bool stopping_ = false;
};

} // namespace forge::net::p2p::detail
