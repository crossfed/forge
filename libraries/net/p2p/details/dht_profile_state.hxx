#pragma once

#include <map>
#include <memory>
#include <vector>

namespace forge::net::p2p::detail {

struct dht_profile_state {
   dht_profile_state(peer_id local, dht::profile profile_value,
                     std::shared_ptr<dht::record_store::persistence> persistence, public_key_resolver public_keys);

   dht::profile profile;
   dht::routing_table routing;
   dht::record_store records;
};

[[nodiscard]] std::map<protocol_id, std::unique_ptr<dht_profile_state>>
make_dht_profile_states(const peer_id& local, std::vector<dht::profile> profiles,
                        const std::map<protocol_id, std::shared_ptr<dht::record_store::persistence>>& persistence,
                        public_key_resolver public_keys);

} // namespace forge::net::p2p::detail
