module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module forge.net.p2p.peer_store;

import forge.net.p2p.dht;
import forge.net.p2p.discovery;
import forge.net.p2p.endpoint;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;
import forge.net.p2p.reachability;
import forge.net.p2p.rendezvous;
import forge.net.p2p.scoring;

export namespace forge::net::p2p {

class peer_store {
 public:
   class persistence;

   struct endpoint_record {
      forge::net::p2p::endpoint endpoint;
      path::kind kind = path::kind::direct;
      std::optional<peer_id> relay_peer;
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
      std::chrono::milliseconds last_latency{0};
      std::chrono::system_clock::time_point backoff_until{};
      double score = 0.0;
   };

   struct relay_record {
      peer_id relay;
      std::uint64_t reservation_id = 0;
      std::chrono::system_clock::time_point expires_at{};
      std::vector<forge::net::p2p::endpoint> endpoints;
      std::vector<std::uint8_t> voucher;
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
      std::chrono::milliseconds last_latency{0};
      double score = 0.0;
   };

   struct record {
      peer_id peer;
      capability_set capabilities{};
      discovery::source discovered_by = discovery::source::explicit_config;
      std::string protocol_version;
      std::string agent_version;
      std::vector<std::uint8_t> public_key;
      std::vector<protocol_id> protocols;
      std::vector<std::uint8_t> signed_peer_record;
      std::vector<endpoint_record> endpoints;
      std::vector<relay_record> relay_reservations;
      reachability::state reachability = reachability::state::unknown;
      std::optional<forge::net::p2p::endpoint> observed_endpoint;
      std::chrono::system_clock::time_point reachability_expires_at{};
      std::chrono::system_clock::time_point discovered_at{};
      std::chrono::system_clock::time_point discovery_expires_at{};
      std::chrono::system_clock::time_point discovery_backoff_until{};
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
      std::chrono::milliseconds last_latency{0};
      double score = 0.0;
   };

   struct provider_record {
      dht::key key;
      dht::peer provider;
      discovery::source discovered_by = discovery::source::dht;
      std::chrono::system_clock::time_point expires_at{};
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
   };

   struct rendezvous_key {
      std::string namespace_name;
      peer_id peer;
   };

   struct mutation_batch {
      std::vector<record> peer_upserts;
      std::vector<peer_id> peer_removals;
      std::vector<provider_record> provider_upserts;
      std::vector<rendezvous::registration> rendezvous_upserts;
      std::vector<rendezvous_key> rendezvous_removals;
      std::uint64_t rendezvous_sequence_high_watermark = 0;
      bool durable = false;
   };

   enum class hydration_kind : std::uint8_t {
      peers,
      providers,
      rendezvous,
   };

   struct hydration_request {
      hydration_kind kind = hydration_kind::peers;
      std::optional<std::vector<std::byte>> cursor;
      std::size_t limit = 256;
   };

   struct hydration_page {
      std::vector<record> peers;
      std::vector<provider_record> providers;
      std::vector<rendezvous::registration> rendezvous_registrations;
      std::uint64_t rendezvous_sequence_high_watermark = 0;
      std::optional<std::vector<std::byte>> cursor;
   };

   struct prune_result {
      std::size_t peers = 0;
      std::size_t providers = 0;
      std::size_t rendezvous_registrations = 0;
      bool may_have_more = false;
   };

   struct persistence_status {
      std::size_t pending_peer_mutations = 0;
      std::uint64_t failure_count = 0;
      bool degraded = false;
      bool closing = false;
      bool closed = false;
      std::string last_failure;
   };

   struct options {
      std::shared_ptr<persistence> persistence;
      std::size_t max_peers = 4'096;
      std::size_t max_providers = 16'384;
      std::size_t max_rendezvous = 16'384;
      std::size_t max_pending = 4'096;
      std::size_t max_endpoints_per_peer = 64;
      std::size_t max_protocols_per_peer = 128;
      std::size_t max_relay_reservations_per_peer = 64;
      std::size_t max_relay_endpoints_per_reservation = 16;
      std::size_t max_peer_record_bytes = 1024 * 1024;
      std::size_t hydration_page_limit = 256;
      std::size_t prune_page_limit = 256;
      std::size_t max_persistence_waiters = 256;
   };

   peer_store();
   explicit peer_store(options options_value);
   ~peer_store();

   peer_store(const peer_store&) = delete;
   peer_store& operator=(const peer_store&) = delete;

   peer_store(peer_store&&) noexcept;
   peer_store& operator=(peer_store&&) noexcept;

   [[nodiscard]] static std::shared_ptr<persistence> make_memory_persistence();

   void upsert(record value);
   void learn_endpoint(peer_id peer, forge::net::p2p::endpoint endpoint, capability_set capabilities = {});
   void mark_reachability(peer_id peer, reachability::state state,
                          std::optional<forge::net::p2p::endpoint> observed = std::nullopt);
   void mark_success(const peer_id& peer, path::kind kind, std::chrono::milliseconds latency);
   void mark_failure(const peer_id& peer);
   void mark_endpoint_success(const peer_id& peer, const forge::net::p2p::endpoint& endpoint, path::kind kind,
                              std::chrono::milliseconds latency);
   void mark_endpoint_failure(const peer_id& peer, const forge::net::p2p::endpoint& endpoint, path::kind kind,
                              std::chrono::system_clock::time_point backoff_until);
   void upsert_routing_peer(dht::peer value, discovery::source source,
                            std::chrono::system_clock::time_point expires_at);

   boost::asio::awaitable<void> async_upsert_provider(provider_record value);
   boost::asio::awaitable<void> async_upsert_rendezvous(rendezvous::registration value);
   boost::asio::awaitable<void> async_remove_rendezvous(peer_id peer, std::string namespace_name);
   boost::asio::awaitable<void> async_hydrate();
   boost::asio::awaitable<prune_result>
   async_prune_expired(std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
   boost::asio::awaitable<void> async_flush();
   boost::asio::awaitable<void> async_close();

   [[nodiscard]] std::optional<record> find(const peer_id& peer) const;
   [[nodiscard]] std::vector<record> snapshot(std::size_t limit) const;
   [[nodiscard]] std::vector<record> candidates(std::uint64_t capability, std::size_t limit) const;
   [[nodiscard]] std::vector<provider_record> find_providers(const dht::key& key, std::size_t limit) const;
   [[nodiscard]] std::vector<rendezvous::registration>
   discover_rendezvous(std::string_view namespace_name, std::uint64_t after_sequence, std::size_t limit) const;
   [[nodiscard]] persistence_status persistence_state() const;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

class peer_store::persistence {
 public:
   virtual ~persistence();

   virtual boost::asio::awaitable<hydration_page> async_hydrate(hydration_request request) = 0;
   virtual boost::asio::awaitable<void> async_apply(mutation_batch batch) = 0;
   virtual boost::asio::awaitable<prune_result> async_prune_expired(std::chrono::system_clock::time_point now,
                                                                    std::size_t limit) = 0;
   virtual boost::asio::awaitable<void> async_flush() = 0;
   virtual boost::asio::awaitable<void> async_close() = 0;
};

} // namespace forge::net::p2p
