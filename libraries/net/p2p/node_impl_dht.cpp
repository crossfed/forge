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
import forge.net.p2p.discovery;
import forge.net.p2p.endpoint;
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

#include "details/dht_exchange.hxx"
#include "details/host_addresses.hxx"
#include "details/node_impl.hxx"
#include "details/operation_deadline.hxx"

namespace forge::net::p2p {

[[nodiscard]] host_addresses::learning_context
discovery_context_for_session_peer(std::optional<peer_id> session_peer, std::optional<endpoint> session_remote_endpoint,
                                   std::optional<endpoint> session_direct_endpoint, const peer_id& peer);

namespace {

[[nodiscard]] dht::peer sanitize_discovered_peer_for_session(dht::peer value, const auto& session) {
   value.endpoints = host_addresses::sanitize_discovered_endpoints(
       std::move(value.endpoints), value.id,
       discovery_context_for_session_peer(session ? std::optional<peer_id>{session->info.remote_peer} : std::nullopt,
                                          session ? session->remote_endpoint : std::nullopt,
                                          session ? session->direct_endpoint : std::nullopt, value.id));
   return value;
}

[[nodiscard]] bool has_usable_endpoint(const dht::peer& value) noexcept {
   return !value.endpoints.empty();
}

} // namespace

void node::impl::increment_dht_query() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.dht_queries;
}

void node::impl::increment_dht_response() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.dht_responses;
}

boost::asio::awaitable<dht::message> node::impl::exchange_dht(const peer_id& peer, dht::message request,
                                                              std::chrono::milliseconds timeout) {
   const auto started = std::chrono::steady_clock::now();
   auto stream = co_await open_protocol_direct(peer, builtins::kad_dht, timeout);
   co_return co_await detail::async_exchange_dht(std::move(stream), std::move(request), options.limits.dht,
                                                 runtime.context(),
                                                 remaining_timeout(started, timeout, "P2P DHT exchange"));
}

boost::asio::awaitable<void> node::impl::send_dht(const peer_id& peer, dht::message request,
                                                  std::chrono::milliseconds timeout) {
   const auto started = std::chrono::steady_clock::now();
   auto stream = co_await open_protocol_direct(peer, builtins::kad_dht, timeout);
   co_await detail::async_send_dht(std::move(stream), std::move(request), options.limits.dht, runtime.context(),
                                   remaining_timeout(started, timeout, "P2P DHT send"));
}

boost::asio::awaitable<void> node::impl::handle_dht(std::shared_ptr<node::impl::session_state> session,
                                                    forge::net::p2p::stream stream) {
   if (!options.capabilities.has(capabilities::dht) || options.limits.dht.operating_mode != dht::mode::server) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "DHT server mode is disabled");
   }
   auto deadline = operation_deadline{runtime.context(), options.limits.dht.query_timeout};
   deadline.arm([&stream] { stream.cancel(); });
   try {
      auto buffer = std::vector<std::uint8_t>{};
      auto request =
          dht::codec::decode(co_await async_read_length_delimited(stream, buffer, options.limits.dht.max_message_size),
                             options.limits.dht);
      increment_dht_query();
      detail::validate_dht_request(request, session->info.remote_peer);

      auto response = dht::message{
          .type = request.type,
          .key_value = request.key_value,
      };
      if (request.type == dht::message_type::find_node) {
         auto closest = routing.closest(request.key_value.bytes, options.limits.dht.replication);
         response.closer_peers.reserve(options.limits.dht.replication);
         const auto append_unique = [&](dht::peer value) {
            const auto current = std::ranges::find_if(response.closer_peers,
                                                      [&](const auto& candidate) { return candidate.id == value.id; });
            if (current != response.closer_peers.end()) {
               for (auto& endpoint : value.endpoints) {
                  const auto known = std::ranges::any_of(current->endpoints, [&](const auto& candidate) {
                     return candidate.to_string() == endpoint.to_string();
                  });
                  if (!known) {
                     current->endpoints.push_back(std::move(endpoint));
                  }
               }
               return;
            }
            if (response.closer_peers.size() >= options.limits.dht.replication) {
               return;
            }
            response.closer_peers.push_back(std::move(value));
         };
         try {
            const auto requested = peer_id::from_bytes(request.key_value.bytes);
            if (requested == local) {
               append_unique(dht::peer{
                   .id = local,
                   .endpoints = local_endpoints_for_control(),
                   .connection = dht::connection_type::connected,
               });
            } else {
               const auto active =
                   std::ranges::find_if(closest, [&](const auto& candidate) { return candidate.id == requested; });
               if (active != closest.end()) {
                  append_unique(*active);
               }
               if (const auto record = store.find(requested)) {
                  auto exact = dht::peer{.id = requested, .connection = dht::connection_type::can_connect};
                  exact.endpoints.reserve(record->endpoints.size());
                  for (const auto& item : record->endpoints) {
                     auto endpoint = item.endpoint;
                     endpoint.peer = requested;
                     exact.endpoints.push_back(std::move(endpoint));
                  }
                  append_unique(std::move(exact));
               }
            }
         } catch (const forge::exceptions::base&) {
            // Arbitrary non-Peer-ID keys remain ordinary closest-node queries.
         }
         for (auto& peer : closest) {
            append_unique(std::move(peer));
         }
      } else if (request.type == dht::message_type::get_providers) {
         const auto providers = store.find_providers(request.key_value, options.limits.dht.max_provider_peers);
         response.provider_peers.reserve(providers.size());
         for (const auto& provider : providers) {
            response.provider_peers.push_back(provider.provider);
         }
         response.closer_peers = routing.closest(request.key_value.bytes, options.limits.dht.replication);
      } else if (request.type == dht::message_type::add_provider) {
         for (const auto& provider : request.provider_peers) {
            auto sanitized = sanitize_discovered_peer_for_session(provider, session);
            if (!has_usable_endpoint(sanitized)) {
               continue;
            }
            co_await store.async_upsert_provider(peer_store::provider_record{
                .key = request.key_value,
                .provider = std::move(sanitized),
                .discovered_by = discovery::source::dht,
                .expires_at = std::chrono::system_clock::now() + options.limits.dht.provider_record_ttl,
            });
         }
         increment_dht_response();
         co_await stream.async_close();
         if (!deadline.finish()) {
            throw_operation_timeout("P2P inbound DHT exchange");
         }
         co_return;
      }
      increment_dht_response();
      co_await stream.async_write(dht::codec::encode(response, options.limits.dht));
      co_await stream.async_close();
      if (!deadline.finish()) {
         throw_operation_timeout("P2P inbound DHT exchange");
      }
   } catch (...) {
      const auto completed = deadline.finish();
      stream.cancel();
      if (deadline.timed_out() || !completed) {
         throw_operation_timeout("P2P inbound DHT exchange");
      }
      throw;
   }
}

} // namespace forge::net::p2p
