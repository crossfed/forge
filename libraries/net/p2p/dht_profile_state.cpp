module;

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <forge/exceptions/macros.hpp>

module forge.net.p2p.node;

import forge.net.p2p.dht;
import forge.net.p2p.dht.record_store;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;

#include "details/dht_profile_state.hxx"

namespace forge::net::p2p::detail {

dht_profile_state::dht_profile_state(peer_id local, dht::profile profile_value,
                                     std::shared_ptr<dht::record_store::persistence> persistence,
                                     public_key_resolver public_keys)
    : profile(std::move(profile_value)), routing(std::move(local), profile.limits),
      records(profile, dht::record_store::options{.persistence = std::move(persistence),
                                                  .public_keys = std::move(public_keys),
                                                  .max_providers_per_key = profile.limits.replication,
                                                  .max_record_bytes = profile.limits.max_record_size}) {}

std::map<protocol_id, std::unique_ptr<dht_profile_state>>
make_dht_profile_states(const peer_id& local, std::vector<dht::profile> profiles,
                        const std::map<protocol_id, std::shared_ptr<dht::record_store::persistence>>& persistence,
                        public_key_resolver public_keys) {
   auto out = std::map<protocol_id, std::unique_ptr<dht_profile_state>>{};
   auto backend_identities = std::set<const dht::record_store::persistence*>{};
   for (auto& profile : profiles) {
      validate(profile);
      const auto protocol = profile.protocol;
      const auto backend = persistence.find(protocol);
      if (backend != persistence.end() && backend->second && !backend_identities.insert(backend->second.get()).second) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P DHT profiles require distinct persistence backends");
      }
      auto state = std::make_unique<dht_profile_state>(
          local, std::move(profile),
          backend == persistence.end() ? std::shared_ptr<dht::record_store::persistence>{} : backend->second,
          public_keys);
      if (!out.emplace(protocol, std::move(state)).second) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P DHT profile protocol IDs must be unique");
      }
   }
   for (const auto& [protocol, _] : persistence) {
      if (!out.contains(protocol)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P DHT persistence is configured for an unknown profile");
      }
   }
   return out;
}

} // namespace forge::net::p2p::detail
