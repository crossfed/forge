#pragma once

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace forge::net::p2p::detail {

class lifecycle_wakeup;

class dht_routing_refresh final {
 public:
   using time_point = std::chrono::steady_clock::time_point;

   struct profile {
      protocol_id protocol;
      dht::routing_table* routing = nullptr;
      std::chrono::milliseconds interval{};
   };

   using query_callback = std::function<boost::asio::awaitable<bool>(protocol_id, dht::key)>;

   struct time_source {
      std::function<time_point()> now;
      std::function<boost::asio::awaitable<std::uint64_t>(std::shared_ptr<lifecycle_wakeup>, std::uint64_t, time_point)>
          wait_until;
   };

   dht_routing_refresh(peer_id local, std::vector<profile> profiles, query_callback query, time_source time = {});
   ~dht_routing_refresh();

   dht_routing_refresh(const dht_routing_refresh&) = delete;
   dht_routing_refresh& operator=(const dht_routing_refresh&) = delete;

   void notify_verified_server() noexcept;
   void request_stop() noexcept;
   boost::asio::awaitable<void> async_run();

 private:
   struct profile_state {
      profile config;
      std::chrono::steady_clock::time_point next_attempt{};
      std::uint64_t generation = 0;
      std::uint32_t failures = 0;
      bool startup_lookup_pending = true;
   };

   [[nodiscard]] dht::key refresh_target(const profile_state& state, std::size_t common_prefix_length) const;
   [[nodiscard]] std::chrono::milliseconds regular_delay(const profile_state& state) const noexcept;
   [[nodiscard]] std::chrono::milliseconds retry_delay(const profile_state& state) const noexcept;
   boost::asio::awaitable<void> async_refresh_profile(profile_state& state);
   [[nodiscard]] bool stopped() const noexcept;

   static constexpr std::size_t max_refresh_common_prefix_length = 15;
   static constexpr std::size_t max_preimage_attempts = 1U << 20U;
   peer_id local_;
   std::vector<profile_state> profiles_;
   query_callback query_;
   time_source time_;
   std::shared_ptr<lifecycle_wakeup> changed_;
   mutable std::mutex mutex_;
   bool stopped_ = false;
};

} // namespace forge::net::p2p::detail
