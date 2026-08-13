#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace forge::net::p2p {

[[nodiscard]] std::shared_ptr<dht::record_store::persistence> make_memory_dht_record_store_persistence();

class memory_dht_record_store_persistence final : public dht::record_store::persistence {
 public:
   boost::asio::awaitable<dht::record_store::hydration_page>
   async_hydrate(dht::record_store::hydration_request request) override;
   boost::asio::awaitable<dht::record_store::apply_result>
   async_apply(dht::record_store::mutation_batch batch) override;
   boost::asio::awaitable<dht::record_store::prune_result>
   async_prune_expired(std::chrono::system_clock::time_point now, std::size_t limit) override;
   boost::asio::awaitable<void> async_flush() override;
   boost::asio::awaitable<void> async_close() override;

   void fail_next_apply_for_testing();

 private:
   using value_key = std::vector<std::uint8_t>;
   using provider_map_key = std::pair<value_key, peer_id>;
   using value_expiry_key = std::pair<std::chrono::system_clock::time_point, value_key>;
   using provider_expiry_key = std::tuple<std::chrono::system_clock::time_point, value_key, peer_id>;

   void ensure_open_locked() const;

   std::mutex mutex_;
   std::map<value_key, dht::record_store::value_record> values_;
   std::map<std::string, value_key> values_by_cursor_;
   std::set<value_expiry_key> values_by_expiry_;
   std::map<provider_map_key, dht::record_store::provider_record> providers_;
   std::map<std::string, provider_map_key> providers_by_cursor_;
   std::set<provider_expiry_key> providers_by_expiry_;
   std::set<provider_expiry_key> provider_addresses_by_expiry_;
   bool fail_next_apply_ = false;
   bool closed_ = false;
};

} // namespace forge::net::p2p
