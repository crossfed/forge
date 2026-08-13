module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
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
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/system_error.hpp>

module forge.net.p2p.node;

import forge.exceptions;
import forge.asio.gate;
import forge.crypto.asymmetric;
import forge.net.p2p.dht;
import forge.net.p2p.diagnostics;
import forge.net.p2p.discovery;
import forge.net.p2p.endpoint;
import forge.net.p2p.envelope;
import forge.net.p2p.exceptions;
import forge.net.p2p.hole_punch;
import forge.net.p2p.identify;
import forge.net.p2p.identity;
import forge.net.p2p.lifecycle;
import forge.net.p2p.negotiation;
import forge.net.p2p.peer_store;
import forge.net.p2p.protocol;
import forge.net.p2p.pubsub;
import forge.net.p2p.reachability;
import forge.net.p2p.relay;
import forge.net.p2p.rendezvous;
import forge.net.p2p.resource_manager;
import forge.net.p2p.scoring;
import forge.net.p2p.stream;
import forge.net.transport.session;
import forge.net.transport.stream;
import forge.net.yamux.session;

#include "details/dht_query.hxx"
#include "details/node_impl.hxx"

namespace forge::net::p2p {

void remember_dht_peer(peer_store& store, dht::routing_table& routing, std::chrono::milliseconds refresh_interval,
                       const dht::peer& value, dht::routing_admission admission);
void mark_dht_failure(peer_store& store, dht::routing_table& routing, const peer_id& peer);
[[nodiscard]] bool remote_peer_attributable_failure(std::mutex& mutex, const bool& stopped,
                                                    const forge::exceptions::base& error);
[[nodiscard]] host_addresses::learning_context third_party_discovery_context();
[[nodiscard]] dht::peer sanitize_discovered_peer(dht::peer value, host_addresses::learning_context context);
[[nodiscard]] bool has_usable_endpoint(const dht::peer& value) noexcept;

namespace {

[[nodiscard]] dht::peer dht_peer_from_record(const peer_store::record& record) {
   auto endpoints = std::vector<endpoint>{};
   endpoints.reserve(record.endpoints.size());
   for (const auto& item : record.endpoints) {
      auto endpoint = item.endpoint;
      endpoint.peer = record.peer;
      endpoints.push_back(std::move(endpoint));
   }
   return dht::peer{
       .id = record.peer, .endpoints = std::move(endpoints), .connection = dht::connection_type::can_connect};
}

} // namespace

boost::asio::awaitable<dht::query_result> node::async_find_peer(peer_id peer) {
   auto self = impl_;
   auto target = make_dht_key(peer);
   const auto query_started = std::chrono::steady_clock::now();
   const auto query_timeout = self->options.limits.dht.query_timeout;
   validate_operation_timeout(query_timeout, "P2P DHT peer lookup timeout");
   auto lookup = co_await dht_query::run(
       dht_query::request{
           .target = target,
           .target_peer = peer,
           .options = self->options.limits.dht,
           .seeds = self->routing.query_seeds(target.bytes, self->options.limits.dht.alpha),
       },
       [self, target, query_started,
        query_timeout](const dht::peer& candidate) -> boost::asio::awaitable<dht::message> {
          co_return co_await self->exchange_dht(candidate.id,
                                                dht::message{
                                                    .type = dht::message_type::find_node,
                                                    .key_value = target,
                                                },
                                                remaining_timeout(query_started, query_timeout, "P2P DHT peer lookup"));
       },
       [self](const dht::peer& candidate, dht::message& response) -> boost::asio::awaitable<void> {
          self->routing.upsert(candidate, dht::routing_admission::verified_server);
          const auto context = third_party_discovery_context();
          for (auto& closer : response.closer_peers) {
             closer = sanitize_discovered_peer(std::move(closer), context);
             if (has_usable_endpoint(closer)) {
                remember_dht_peer(self->store, self->routing, self->options.limits.dht.refresh_interval, closer,
                                  dht::routing_admission::candidate);
             }
          }
          response.closer_peers.erase(std::remove_if(response.closer_peers.begin(), response.closer_peers.end(),
                                                     [](const auto& peer) { return !has_usable_endpoint(peer); }),
                                      response.closer_peers.end());
          co_return;
       },
       [self](const dht::peer&, const forge::exceptions::base& error) {
          return remote_peer_attributable_failure(self->mutex, self->stopped, error);
       });
   for (const auto& failed : lookup.failed) {
      mark_dht_failure(self->store, self->routing, failed);
   }
   (void)remaining_timeout(query_started, query_timeout, "P2P DHT peer lookup");
   if (lookup.query.complete) {
      if (const auto record = self->store.find(peer)) {
         auto exact = dht_peer_from_record(*record);
         if (has_usable_endpoint(exact)) {
            const auto current = std::ranges::find_if(lookup.query.closest_peers,
                                                      [&](const auto& candidate) { return candidate.id == peer; });
            if (current != lookup.query.closest_peers.end()) {
               for (auto& endpoint : current->endpoints) {
                  const auto known = std::ranges::any_of(exact.endpoints, [&](const auto& candidate) {
                     return candidate.to_string() == endpoint.to_string();
                  });
                  if (!known) {
                     exact.endpoints.push_back(std::move(endpoint));
                  }
               }
               *current = std::move(exact);
            } else {
               lookup.query.closest_peers.insert(lookup.query.closest_peers.begin(), std::move(exact));
               if (lookup.query.closest_peers.size() > self->options.limits.dht.replication) {
                  lookup.query.closest_peers.pop_back();
               }
            }
         }
      }
   }
   co_return lookup.query;
}

boost::asio::awaitable<void> node::async_provide(dht::key key) {
   auto self = impl_;
   auto endpoints = self->local_endpoints_for_control();
   auto provider = dht::peer{.id = self->local, .endpoints = endpoints, .connection = dht::connection_type::connected};
   co_await self->store.async_upsert_provider(peer_store::provider_record{
       .key = key,
       .provider = provider,
       .discovered_by = discovery::source::dht,
       .expires_at = std::chrono::system_clock::now() + self->options.limits.dht.provider_record_ttl,
   });
   const auto query_started = std::chrono::steady_clock::now();
   const auto query_timeout = self->options.limits.dht.query_timeout;
   validate_operation_timeout(query_timeout, "P2P DHT provide timeout");
   auto lookup = co_await dht_query::run(
       dht_query::request{
           .target = key,
           .options = self->options.limits.dht,
           .seeds = self->routing.query_seeds(key.bytes, self->options.limits.dht.alpha),
       },
       [self, key, query_started, query_timeout](const dht::peer& candidate) -> boost::asio::awaitable<dht::message> {
          co_return co_await self->exchange_dht(candidate.id,
                                                dht::message{
                                                    .type = dht::message_type::find_node,
                                                    .key_value = key,
                                                },
                                                remaining_timeout(query_started, query_timeout, "P2P DHT provide"));
       },
       [self](const dht::peer& candidate, dht::message& response) -> boost::asio::awaitable<void> {
          self->routing.upsert(candidate, dht::routing_admission::verified_server);
          const auto context = third_party_discovery_context();
          for (auto& closer : response.closer_peers) {
             closer = sanitize_discovered_peer(std::move(closer), context);
             if (has_usable_endpoint(closer)) {
                remember_dht_peer(self->store, self->routing, self->options.limits.dht.refresh_interval, closer,
                                  dht::routing_admission::candidate);
             }
          }
          response.closer_peers.erase(std::remove_if(response.closer_peers.begin(), response.closer_peers.end(),
                                                     [](const auto& peer) { return !has_usable_endpoint(peer); }),
                                      response.closer_peers.end());
          co_return;
       },
       [self](const dht::peer&, const forge::exceptions::base& error) {
          return remote_peer_attributable_failure(self->mutex, self->stopped, error);
       });
   for (const auto& failed : lookup.failed) {
      mark_dht_failure(self->store, self->routing, failed);
   }
   (void)remaining_timeout(query_started, query_timeout, "P2P DHT provide");
   auto candidates = lookup.query.closest_peers;
   if (candidates.empty()) {
      candidates = self->routing.closest(key.bytes, self->options.limits.dht.replication);
   }
   for (const auto& candidate : candidates) {
      const auto remaining = remaining_timeout(query_started, query_timeout, "P2P DHT provide");
      try {
         co_await self->send_dht(candidate.id,
                                 dht::message{
                                     .type = dht::message_type::add_provider,
                                     .key_value = key,
                                     .provider_peers = std::vector<dht::peer>{provider},
                                 },
                                 remaining);
      } catch (const forge::exceptions::base& error) {
         if (!remote_peer_attributable_failure(self->mutex, self->stopped, error)) {
            throw;
         }
         mark_dht_failure(self->store, self->routing, candidate.id);
         (void)remaining_timeout(query_started, query_timeout, "P2P DHT provide");
      }
   }
}

boost::asio::awaitable<std::vector<dht::peer>> node::async_find_providers(dht::key key) {
   auto self = impl_;
   auto out = std::vector<dht::peer>{};
   for (const auto& provider : self->store.find_providers(key, self->options.limits.dht.max_provider_peers)) {
      out.push_back(provider.provider);
   }
   if (!out.empty()) {
      co_return out;
   }
   const auto query_started = std::chrono::steady_clock::now();
   const auto query_timeout = self->options.limits.dht.query_timeout;
   validate_operation_timeout(query_timeout, "P2P DHT provider lookup timeout");
   auto lookup = co_await dht_query::run(
       dht_query::request{
           .target = key,
           .options = self->options.limits.dht,
           .seeds = self->routing.query_seeds(key.bytes, self->options.limits.dht.alpha),
       },
       [self, key, query_started, query_timeout](const dht::peer& candidate) -> boost::asio::awaitable<dht::message> {
          co_return co_await self->exchange_dht(
              candidate.id,
              dht::message{
                  .type = dht::message_type::get_providers,
                  .key_value = key,
              },
              remaining_timeout(query_started, query_timeout, "P2P DHT provider lookup"));
       },
       [self, key](const dht::peer& candidate, dht::message& response) -> boost::asio::awaitable<void> {
          self->routing.upsert(candidate, dht::routing_admission::verified_server);
          const auto context = third_party_discovery_context();
          for (auto& provider : response.provider_peers) {
             provider = sanitize_discovered_peer(std::move(provider), context);
             if (has_usable_endpoint(provider)) {
                co_await self->store.async_upsert_provider(peer_store::provider_record{
                    .key = key,
                    .provider = provider,
                    .discovered_by = discovery::source::dht,
                    .expires_at = std::chrono::system_clock::now() + self->options.limits.dht.provider_record_ttl,
                });
             }
          }
          for (auto& closer : response.closer_peers) {
             closer = sanitize_discovered_peer(std::move(closer), context);
             if (has_usable_endpoint(closer)) {
                remember_dht_peer(self->store, self->routing, self->options.limits.dht.refresh_interval, closer,
                                  dht::routing_admission::candidate);
             }
          }
          response.closer_peers.erase(std::remove_if(response.closer_peers.begin(), response.closer_peers.end(),
                                                     [](const auto& peer) { return !has_usable_endpoint(peer); }),
                                      response.closer_peers.end());
          response.provider_peers.erase(std::remove_if(response.provider_peers.begin(), response.provider_peers.end(),
                                                       [](const auto& peer) { return !has_usable_endpoint(peer); }),
                                        response.provider_peers.end());
          co_return;
       },
       [self](const dht::peer&, const forge::exceptions::base& error) {
          return remote_peer_attributable_failure(self->mutex, self->stopped, error);
       });
   for (const auto& failed : lookup.failed) {
      mark_dht_failure(self->store, self->routing, failed);
   }
   co_return lookup.query.provider_peers;
}

} // namespace forge::net::p2p
