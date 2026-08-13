module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.net.p2p.node;

import forge.net.p2p.dht;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;

#include "details/dht_query.hxx"

namespace forge::net::p2p::dht_query {

bool has_endpoint(const dht::peer& value) noexcept {
   return !value.endpoints.empty();
}

void merge_peer(dht::peer& target, const dht::peer& source) {
   if (target.id.value.empty()) {
      target.id = source.id;
   }
   target.connection = source.connection;
   for (const auto& endpoint : source.endpoints) {
      const auto exists = std::ranges::any_of(
          target.endpoints, [&](const auto& current) { return current.to_string() == endpoint.to_string(); });
      if (!exists) {
         target.endpoints.push_back(endpoint);
      }
   }
}

void merge_known(std::map<peer_id, dht::peer>& known, const dht::peer& value, std::size_t limit) {
   if (!valid_peer_id(value.id)) {
      return;
   }
   const auto found = known.find(value.id);
   if (found != known.end()) {
      merge_peer(found->second, value);
      return;
   }
   if (known.size() >= limit) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT query discovered-peer limit reached");
   }
   auto [inserted, _] = known.emplace(value.id, dht::peer{});
   merge_peer(inserted->second, value);
}

void merge_provider(std::vector<dht::peer>& providers, const dht::peer& value) {
   if (!valid_peer_id(value.id)) {
      return;
   }
   const auto found = std::ranges::find_if(providers, [&](const auto& current) { return current.id == value.id; });
   if (found == providers.end()) {
      providers.push_back(value);
      return;
   }
   merge_peer(*found, value);
}

std::vector<dht::peer> sorted_peers(const std::map<peer_id, dht::peer>& known, const dht::key& target) {
   auto out = std::vector<dht::peer>{};
   out.reserve(known.size());
   for (const auto& [_, peer] : known) {
      out.push_back(peer);
   }
   std::ranges::sort(out, [&](const auto& left, const auto& right) {
      const auto left_distance = distance_between(left.id.to_bytes(), target.bytes);
      const auto right_distance = distance_between(right.id.to_bytes(), target.bytes);
      if (left_distance != right_distance) {
         return left_distance < right_distance;
      }
      return left.id.to_string() < right.id.to_string();
   });
   return out;
}

std::optional<dht::peer> next_peer(const std::map<peer_id, dht::peer>& known, const std::set<peer_id>& attempted,
                                   const dht::key& target) {
   for (const auto& peer : sorted_peers(known, target)) {
      if (!has_endpoint(peer) || attempted.contains(peer.id)) {
         continue;
      }
      return peer;
   }
   return std::nullopt;
}

bool closest_peers_queried(const std::map<peer_id, dht::peer>& known, const std::set<peer_id>& queried,
                           const std::set<peer_id>& failed, const dht::key& target, std::size_t replication) {
   auto considered = std::size_t{};
   for (const auto& peer : sorted_peers(known, target)) {
      if (!has_endpoint(peer) || failed.contains(peer.id)) {
         continue;
      }
      if (considered >= replication) {
         break;
      }
      ++considered;
      if (!queried.contains(peer.id)) {
         return false;
      }
   }
   return considered > 0;
}

} // namespace forge::net::p2p::dht_query
