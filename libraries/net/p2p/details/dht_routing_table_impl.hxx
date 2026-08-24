#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace forge::net::p2p {

struct dht::routing_table::impl {
   struct entry {
      dht::peer value;
      dht::routing_admission admission = dht::routing_admission::candidate;
      std::size_t failures = 0;
      std::uint64_t sequence = 0;
   };

   struct bucket {
      std::vector<entry> active;
      std::vector<entry> replacements;
      std::chrono::steady_clock::time_point refreshed_at{};
      std::uint64_t generation = 1;
   };

   impl(peer_id local_peer, dht::options options_value);

   [[nodiscard]] std::size_t bucket_for(const peer_id& peer) const;
   void upsert(dht::peer value, dht::routing_admission admission);
   void remove(const peer_id& peer);
   void mark_failure(const peer_id& peer);
   [[nodiscard]] std::vector<dht::peer> closest(std::span<const std::uint8_t> target, std::size_t limit) const;
   [[nodiscard]] std::vector<dht::peer> query_seeds(std::span<const std::uint8_t> target, std::size_t limit) const;
   [[nodiscard]] std::vector<dht::peer> snapshot() const;
   [[nodiscard]] dht::routing_status status() const;
   [[nodiscard]] std::vector<dht::routing_refresh_bucket> plan_refresh(std::chrono::steady_clock::time_point now,
                                                                       std::chrono::milliseconds stale_after) const;
   [[nodiscard]] bool mark_refreshed(dht::routing_refresh_bucket bucket,
                                     std::chrono::steady_clock::time_point refreshed_at);
   static void advance_generation(bucket& value) noexcept;

   peer_id local;
   dht::options options;
   std::array<bucket, 256> buckets;
   mutable std::mutex mutex;
   std::uint64_t next_sequence = 1;
};

} // namespace forge::net::p2p
