module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
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

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.asio.gate;
import forge.crypto.asymmetric;
import forge.net.p2p.dht;
import forge.net.p2p.discovery;
import forge.net.p2p.endpoint;
import forge.net.p2p.identify;
import forge.net.p2p.exceptions;
import forge.net.p2p.lifecycle;
import forge.net.p2p.negotiation;
import forge.net.p2p.peer_store;
import forge.net.p2p.pubsub;
import forge.net.p2p.relay;
import forge.net.p2p.resource_manager;
import forge.net.transport.session;
import forge.net.transport.stream;
import forge.net.yamux.session;

#include "details/bootstrap_service.hxx"
#include "details/node_impl.hxx"

namespace forge::net::p2p {
namespace {

[[nodiscard]] bool lifecycle_queryable(const peer_store::record& record) noexcept {
   return !record.endpoints.empty() && (record.discovery_backoff_until == std::chrono::system_clock::time_point{} ||
                                        record.discovery_backoff_until <= std::chrono::system_clock::now());
}

[[nodiscard]] dht::peer lifecycle_dht_peer(const peer_store::record& record) {
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

void validate_bootstrap(const std::vector<bootstrap_peer>& peers, bool require_nonempty) {
   if (require_nonempty && peers.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                            "P2P bootstrap connection is required but no bootstrap peers are configured");
   }
   auto keys = std::set<std::string>{};
   for (const auto& peer : peers) {
      if (peer.address.peer && !valid_peer_id(*peer.address.peer)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid P2P bootstrap peer id");
      }
      if (!keys.insert(peer.address.to_string()).second) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "duplicate P2P bootstrap endpoint");
      }
   }
}

void node::impl::initialize_lifecycle() {
   const auto weak = weak_from_this();
   bootstrap = std::make_shared<detail::bootstrap_service>(
       runtime.context().get_executor(), options.lifecycle,
       detail::bootstrap_service::callbacks{
           .connect = [weak](bootstrap_peer peer,
                             std::chrono::milliseconds timeout) -> boost::asio::awaitable<peer_id> {
              const auto self = weak.lock();
              if (!self) {
                 FORGE_THROW_EXCEPTION(exceptions::closed, "P2P node no longer owns bootstrap state");
              }
              const auto expected_peer = peer.address.peer;
              const auto session =
                  co_await self->connect_direct(std::move(peer.address), node::connect_options{
                                                                             .expected_peer = expected_peer,
                                                                             .allow_relay = false,
                                                                             .timeout = timeout,
                                                                             .direct_attempt_timeout = timeout,
                                                                             .allow_hole_punch = false,
                                                                         });
              co_return session->info.remote_peer;
           },
           .connected =
               [weak](const bootstrap_peer& configured, const peer_id& peer) {
                  const auto self = weak.lock();
                  if (!self) {
                     return false;
                  }
                  const auto endpoint = configured.address.to_string();
                  const auto lock = std::scoped_lock{self->mutex};
                  return std::ranges::any_of(self->sessions, [&](const auto& item) {
                     const auto& session = item.second;
                     return !session->closed && session->info.remote_peer == peer &&
                            session->info.path == path::kind::direct && session->direct_endpoint &&
                            session->direct_endpoint->to_string() == endpoint;
                  });
               },
           .protect =
               [weak](const peer_id& peer) {
                  if (const auto self = weak.lock()) {
                     const auto lock = std::scoped_lock{self->mutex};
                     if (!self->stopped) {
                        self->connections.protect(peer, "bootstrap");
                     }
                  }
               },
           .unprotect =
               [weak](const peer_id& peer) {
                  if (const auto self = weak.lock()) {
                     const auto lock = std::scoped_lock{self->mutex};
                     static_cast<void>(self->connections.unprotect(peer, "bootstrap"));
                  }
               },
           .prune_peer_state = [weak]() -> boost::asio::awaitable<void> {
              const auto self = weak.lock();
              if (!self) {
                 co_return;
              }
              static_cast<void>(co_await self->store.async_prune_expired());
           },
       });
}

void node::impl::request_lifecycle_stop() noexcept {
   bootstrap->request_stop();
   identify_service.close();
   session_admission_gate.close();
   lifecycle.request_stop();
}

boost::asio::awaitable<void> node::impl::async_hydrate_peer_state() {
   co_await store.async_hydrate();
   for (const auto& record : store.candidates(capabilities::dht, options.peer_state.max_peers)) {
      if (record.peer != local && lifecycle_queryable(record)) {
         routing.upsert(lifecycle_dht_peer(record), dht::routing_admission::candidate);
      }
   }
}

void node::impl::listen(forge::net::p2p::endpoint endpoint) {
   auto local_endpoint = forge::net::p2p::endpoint{};
   {
      const auto lock = std::scoped_lock{mutex};
      if (stopped) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "P2P node is stopped");
      }
      local_endpoint = direct_registry.listen(std::move(endpoint));
   }
   launch_accept_loop(std::move(local_endpoint));
   launch_pubsub_heartbeat();
   launch_relay_discovery_maintenance();
}

boost::asio::awaitable<lifecycle_status> node::impl::async_start_lifecycle() {
   co_await async_hydrate_peer_state();
   lifecycle.set_phase(lifecycle_phase::listening);
   for (const auto& endpoint : options.lifecycle.listen) {
      listen(endpoint);
   }

   lifecycle.set_phase(lifecycle_phase::bootstrapping);
   const auto connected = co_await bootstrap->async_initial_bootstrap();
   if (options.lifecycle.requirement == bootstrap_requirement::require_connection && connected == 0) {
      FORGE_THROW_EXCEPTION(exceptions::timeout,
                            "P2P initial bootstrap did not establish a required connection within startup budget");
   }

   lifecycle.set_phase(lifecycle_phase::maintenance);
   bootstrap->start_maintenance(lifecycle);
   co_return lifecycle_status{
       .phase = lifecycle_phase::maintenance,
       .requirement = options.lifecycle.requirement,
       .configured_bootstrap = bootstrap->configured_count(),
       .connected_bootstrap = connected,
       .degraded = bootstrap->configured_count() != 0 && connected == 0,
       .last_bootstrap_failure = bootstrap->last_failure(),
   };
}

} // namespace forge::net::p2p
