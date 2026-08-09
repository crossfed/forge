#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace forge::net::p2p {

class memory_peer_store_persistence final : public peer_store::persistence {
 public:
   boost::asio::awaitable<peer_store::hydration_page> async_hydrate(peer_store::hydration_request request) override;
   boost::asio::awaitable<void> async_apply(peer_store::mutation_batch batch) override;
   boost::asio::awaitable<peer_store::prune_result> async_prune_expired(std::chrono::system_clock::time_point now,
                                                                        std::size_t limit) override;
   boost::asio::awaitable<void> async_flush() override;
   boost::asio::awaitable<void> async_close() override;

 private:
   using provider_key = std::pair<std::vector<std::uint8_t>, peer_id>;
   using rendezvous_map_key = std::pair<std::string, peer_id>;
   using peer_expiry_key = std::pair<std::chrono::system_clock::time_point, peer_id>;
   using provider_expiry_key = std::tuple<std::chrono::system_clock::time_point, std::vector<std::uint8_t>, peer_id>;
   using rendezvous_expiry_key = std::pair<std::chrono::system_clock::time_point, rendezvous_map_key>;

   void ensure_open_locked() const;

   std::mutex mutex_;
   std::map<peer_id, peer_store::record> peers_;
   std::map<std::string, peer_id> peers_by_cursor_;
   std::set<peer_expiry_key> peers_by_expiry_;
   std::map<provider_key, peer_store::provider_record> providers_;
   std::map<std::string, provider_key> providers_by_cursor_;
   std::set<provider_expiry_key> providers_by_expiry_;
   std::map<rendezvous_map_key, rendezvous::registration> rendezvous_;
   std::map<std::string, rendezvous_map_key> rendezvous_by_cursor_;
   std::set<rendezvous_expiry_key> rendezvous_by_expiry_;
   std::uint64_t rendezvous_sequence_high_watermark_ = 0;
   bool closed_ = false;
};

} // namespace forge::net::p2p
