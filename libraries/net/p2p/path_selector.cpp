module;

#include <algorithm>
#include <chrono>
#include <vector>

module forge.net.p2p.node;

import forge.net.p2p.endpoint;
import forge.net.p2p.peer_store;
import forge.net.p2p.scoring;
import forge.exceptions;
import forge.multiformats.multiaddr;

#include "details/path_selector.hxx"

namespace forge::net::p2p::path_selector {

bool supported_direct(const forge::multiformats::multiaddr& address) {
   try {
      const auto endpoint = parse_endpoint(address.to_string());
      return endpoint.is_direct_quic() || endpoint.is_direct_tcp();
   } catch (const forge::exceptions::base&) {
      const auto& components = address.components();
      return std::ranges::any_of(components, [](const auto& component) {
                return component.code == forge::multiformats::protocol_code::dnsaddr;
             }) &&
             std::ranges::none_of(components, [](const auto& component) {
                return component.code == forge::multiformats::protocol_code::p2p_circuit;
             });
   }
}

std::vector<peer_store::endpoint_record> rank_direct(const peer_store::record& record,
                                                     std::chrono::system_clock::time_point now) {
   auto fresh = std::vector<peer_store::endpoint_record>{};
   auto backed_off = std::vector<peer_store::endpoint_record>{};
   for (const auto& endpoint : record.endpoints) {
      if (endpoint.kind != path::kind::direct || endpoint.relay_peer || !supported_direct(endpoint.address)) {
         continue;
      }
      if (endpoint.backoff_until != std::chrono::system_clock::time_point{} && endpoint.backoff_until > now) {
         backed_off.push_back(endpoint);
      } else {
         fresh.push_back(endpoint);
      }
   }

   auto& selected = fresh.empty() ? backed_off : fresh;
   std::stable_sort(selected.begin(), selected.end(),
                    [](const auto& left, const auto& right) { return left.score > right.score; });
   return selected;
}

} // namespace forge::net::p2p::path_selector
