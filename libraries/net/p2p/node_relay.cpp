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
#include "details/libp2p_identity_material.hxx"
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
[[nodiscard]] std::optional<rendezvous::registration>
sanitize_discovered_registration(rendezvous::registration registration, host_addresses::learning_context context);

namespace {

[[nodiscard]] bool supports(const peer_store::record& record, std::uint64_t capability) noexcept {
   return record.capabilities.has(capability);
}

[[nodiscard]] bool queryable(const peer_store::record& record) noexcept {
   return !record.endpoints.empty() && (record.discovery_backoff_until == std::chrono::system_clock::time_point{} ||
                                        record.discovery_backoff_until <= std::chrono::system_clock::now());
}

[[nodiscard]] dht::peer routing_candidate(const peer_store::record& record) {
   auto endpoints = std::vector<endpoint>{};
   endpoints.reserve(record.endpoints.size());
   for (const auto& item : record.endpoints) {
      auto value = item.endpoint;
      value.peer = record.peer;
      endpoints.push_back(std::move(value));
   }
   return dht::peer{
       .id = record.peer,
       .endpoints = std::move(endpoints),
       .connection = dht::connection_type::can_connect,
   };
}

[[nodiscard]] std::vector<peer_store::record> discovery_records(std::span<const peer_store::record> records,
                                                                std::uint64_t capability, std::size_t limit) {
   auto out = std::vector<peer_store::record>{};
   if (limit == 0) {
      return out;
   }
   for (const auto& record : records) {
      if (!valid_peer_id(record.peer) || !supports(record, capability) || !queryable(record)) {
         continue;
      }
      out.push_back(record);
   }
   std::stable_sort(out.begin(), out.end(), [](const auto& left, const auto& right) {
      if (left.score != right.score) {
         return left.score > right.score;
      }
      return left.peer.to_string() < right.peer.to_string();
   });
   if (out.size() > limit) {
      out.resize(limit);
   }
   return out;
}

void append_result(std::vector<discovery::result>& out, const peer_store::record& record, discovery::source source,
                   std::chrono::system_clock::time_point expires_at, std::size_t limit) {
   if (record.peer.value.empty() || out.size() >= limit) {
      return;
   }
   const auto exists = std::ranges::any_of(out, [&](const auto& current) { return current.peer == record.peer; });
   if (exists) {
      return;
   }
   auto endpoints = std::vector<endpoint>{};
   endpoints.reserve(record.endpoints.size());
   for (const auto& item : record.endpoints) {
      endpoints.push_back(item.endpoint);
   }
   out.push_back(discovery::result{
       .peer = record.peer,
       .endpoints = std::move(endpoints),
       .capabilities = record.capabilities,
       .discovered_by = source,
       .preferred_path = path::kind::direct,
       .expires_at = expires_at,
       .score = record.score,
   });
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> make_local_rendezvous_record(const auto& self,
                                                                                    std::uint64_t sequence) {
   if (self.options.public_key.empty() || self.options.private_key_pem.empty()) {
      return std::nullopt;
   }
   auto endpoints = self.local_endpoints_for_control();
   if (endpoints.empty()) {
      return std::nullopt;
   }
   return rendezvous::codec::seal_peer_record(
              rendezvous::peer_record{
                  .peer = self.local,
                  .endpoints = std::move(endpoints),
                  .sequence = sequence,
              },
              decode_public_key(self.identity.public_key), require_libp2p_identity_private_key(self.identity))
       .encode();
}

} // namespace

boost::asio::awaitable<relay::reservation::info> node::async_reserve_relay(peer_id relay_peer) {
   return async_reserve_relay(std::move(relay_peer), relay::reservation::options{});
}

boost::asio::awaitable<relay::reservation::info> node::async_reserve_relay(peer_id relay_peer,
                                                                           relay::reservation::options options) {
   auto self = impl_;
   co_return co_await self->request_relay_reservation(relay_peer, options, node::connect_options{}.timeout);
}

boost::asio::awaitable<std::vector<relay::reservation::info>> node::async_refresh_relay_candidates() {
   auto self = impl_;
   co_return co_await self->refresh_relay_candidates(std::nullopt, self->options.limits.discovery.query_timeout);
}

boost::asio::awaitable<std::vector<discovery::result>> node::async_refresh_discovery() {
   auto self = impl_;
   validate_operation_timeout(self->options.limits.discovery.query_timeout, "P2P discovery refresh timeout");
   if (!self->options.limits.discovery.enabled) {
      co_return std::vector<discovery::result>{};
   }

   const auto now = std::chrono::system_clock::now();
   const auto expires_at = now + self->options.limits.discovery.refresh_interval;
   const auto query_started = std::chrono::steady_clock::now();
   const auto query_timeout = self->options.limits.discovery.query_timeout;
   auto out = std::vector<discovery::result>{};
   out.reserve(self->options.limits.discovery.max_results);

   if (self->options.limits.discovery.dht_enabled) {
      const auto target = make_dht_key(self->local);
      auto seeds = self->routing.query_seeds(target.bytes, self->options.limits.discovery.max_parallel_queries);
      if (seeds.empty()) {
         for (const auto& record :
              self->store.candidates(capabilities::dht, self->options.limits.discovery.max_parallel_queries)) {
            if (record.peer != self->local && queryable(record)) {
               self->routing.upsert(routing_candidate(record), dht::routing_admission::candidate);
               (void)co_await self->identify_peer_for_discovery(
                   record.peer, record.discovered_by,
                   remaining_timeout(query_started, query_timeout, "P2P discovery refresh"));
            }
         }
         (void)remaining_timeout(query_started, query_timeout, "P2P discovery refresh");
         seeds = self->routing.query_seeds(target.bytes, self->options.limits.discovery.max_parallel_queries);
      }
      if (!seeds.empty()) {
         auto lookup = co_await dht_query::run(
             dht_query::request{
                 .target = target,
                 .options = self->options.limits.dht,
                 .seeds = std::move(seeds),
             },
             [self, target, query_started,
              query_timeout](const dht::peer& candidate) -> boost::asio::awaitable<dht::message> {
                co_return co_await self->exchange_dht(
                    candidate.id,
                    dht::message{
                        .type = dht::message_type::find_node,
                        .key_value = target,
                    },
                    remaining_timeout(query_started, query_timeout, "P2P discovery refresh"));
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
         (void)remaining_timeout(query_started, query_timeout, "P2P discovery refresh");
         for (const auto& peer : lookup.query.closest_peers) {
            if (peer.id == self->local || out.size() >= self->options.limits.discovery.max_results) {
               continue;
            }
            (void)co_await self->identify_peer_for_discovery(
                peer.id, discovery::source::dht,
                remaining_timeout(query_started, query_timeout, "P2P discovery refresh"));
            if (const auto record = self->store.find(peer.id)) {
               append_result(out, *record, discovery::source::dht, expires_at,
                             self->options.limits.discovery.max_results);
            }
         }
         (void)remaining_timeout(query_started, query_timeout, "P2P discovery refresh");
      }
   }

   if (self->options.limits.discovery.rendezvous_enabled) {
      const auto rendezvous_candidates =
          self->store.candidates(capabilities::rendezvous, self->options.limits.discovery.max_parallel_queries);
      for (const auto& record : discovery_records(rendezvous_candidates, capabilities::rendezvous,
                                                  self->options.limits.discovery.max_parallel_queries)) {
         if (record.peer == self->local) {
            continue;
         }
         for (const auto& namespace_name : self->options.limits.discovery.rendezvous_namespaces) {
            if (namespace_name.empty() || out.size() >= self->options.limits.discovery.max_results) {
               continue;
            }
            if (auto signed_record = make_local_rendezvous_record(*self, random_nonce())) {
               try {
                  (void)co_await async_rendezvous_register(record.peer,
                                                           rendezvous::register_request{
                                                               .namespace_name = namespace_name,
                                                               .signed_peer_record = std::move(*signed_record),
                                                               .ttl = self->options.limits.rendezvous.default_ttl,
                                                           });
               } catch (const forge::exceptions::base& error) {
                  if (remote_peer_attributable_failure(self->mutex, self->stopped, error)) {
                     self->store.mark_failure(record.peer);
                  }
               }
            }

            auto cookie = std::vector<std::uint8_t>{};
            {
               auto lock = std::scoped_lock{self->mutex};
               const auto it = self->discovery_value.rendezvous_cookies.find({record.peer, namespace_name});
               if (it != self->discovery_value.rendezvous_cookies.end()) {
                  cookie = it->second;
               }
            }

            try {
               auto response = co_await async_rendezvous_discover(
                   record.peer, rendezvous::discover_request{
                                    .namespace_name = namespace_name,
                                    .limit = self->options.limits.discovery.max_results,
                                    .cookie = std::move(cookie),
                                });
               {
                  auto lock = std::scoped_lock{self->mutex};
                  self->discovery_value.rendezvous_cookies[{record.peer, namespace_name}] = response.cookie;
               }
               for (const auto& registration : response.registrations) {
                  if (registration.peer == self->local || out.size() >= self->options.limits.discovery.max_results) {
                     continue;
                  }
                  auto sanitized = sanitize_discovered_registration(registration, third_party_discovery_context());
                  if (!sanitized) {
                     continue;
                  }
                  for (const auto& endpoint : sanitized->endpoints) {
                     self->store.learn_endpoint(registration.peer, endpoint);
                  }
                  (void)co_await self->identify_peer_for_discovery(registration.peer, discovery::source::rendezvous,
                                                                   self->options.limits.discovery.query_timeout);
                  if (const auto learned = self->store.find(registration.peer)) {
                     append_result(out, *learned, discovery::source::rendezvous, registration.expires_at,
                                   self->options.limits.discovery.max_results);
                  }
               }
            } catch (const forge::exceptions::base& error) {
               if (remote_peer_attributable_failure(self->mutex, self->stopped, error)) {
                  self->store.mark_failure(record.peer);
               }
            }
         }
      }
   }

   co_return out;
}

boost::asio::awaitable<void> node::async_cancel_relay(peer_id relay_peer) {
   auto self = impl_;
   {
      auto lock = std::scoped_lock{self->mutex};
      self->cleanup_expired_relay_reservations_locked();
      const auto it = self->outbound_relay_reservations.find(relay_peer);
      if (it == self->outbound_relay_reservations.end()) {
         co_return;
      }
      self->outbound_relay_reservations.erase(it);
   }
}

boost::asio::awaitable<hole_punch::status>
node::async_attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer, std::chrono::milliseconds timeout) {
   auto self = impl_;
   co_return co_await self->attempt_hole_punch(std::move(peer), std::move(relay_peer), timeout);
}

} // namespace forge::net::p2p
