module;

#include <chrono>
#include <cstdint>
#include <vector>

export module forge.net.p2p.discovery;

import forge.net.p2p.endpoint;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;
import forge.net.p2p.scoring;

export namespace forge::net::p2p {

struct discovery {
   enum class source : std::uint16_t {
      explicit_config = 0,
      identify = 1,
      dht = 2,
      rendezvous = 3,
      peer_exchange = 4,
   };

   struct result {
      peer_id peer;
      std::vector<endpoint> endpoints;
      capability_set capabilities{};
      source discovered_by = source::explicit_config;
      path::kind preferred_path = path::kind::direct;
      std::chrono::system_clock::time_point expires_at{};
      double score = 0.0;
   };

   struct observation {
      peer_id peer;
      source discovered_by = source::explicit_config;
      std::chrono::system_clock::time_point observed_at{};
      std::chrono::system_clock::time_point expires_at{};
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
      std::chrono::system_clock::time_point backoff_until{};
      double score = 0.0;
   };
};

} // namespace forge::net::p2p
