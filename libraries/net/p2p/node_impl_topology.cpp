module;

#include <forge/exceptions/macros.hpp>

#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
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

import forge.asio.gate;
import forge.asio.notification;
import forge.crypto.asymmetric;
import forge.net.p2p.dht;
import forge.net.p2p.discovery;
import forge.net.p2p.endpoint;
import forge.net.p2p.envelope;
import forge.net.p2p.exceptions;
import forge.net.p2p.identify;
import forge.net.p2p.peer_store;
import forge.net.p2p.rendezvous;
import forge.net.p2p.topology;
import forge.net.transport.session;
import forge.net.transport.stream;
import forge.net.yamux.session;

#include "details/cancellation_latch.hxx"
#include "details/lifecycle_wakeup.hxx"
#include "details/node_impl.hxx"

namespace forge::net::p2p {

[[nodiscard]] host_addresses::learning_context third_party_discovery_context();
[[nodiscard]] std::optional<rendezvous::registration>
sanitize_discovered_registration(rendezvous::registration registration, host_addresses::learning_context context);

namespace {

void append_topology_result(std::vector<discovery::result>& out, const peer_store::record& record,
                            discovery::source source, std::chrono::system_clock::time_point expires_at,
                            std::size_t limit) {
   if (record.peer.value.empty() || out.size() >= limit) {
      return;
   }
   const auto exists = std::ranges::any_of(
       out, [&](const auto& current) { return current.peer == record.peer && current.discovered_by == source; });
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

void append_topology_hint(std::vector<discovery::result>& out, const peer_id& peer, std::vector<endpoint> endpoints,
                          discovery::source source, std::chrono::system_clock::time_point expires_at,
                          std::size_t limit) {
   if (!valid_peer_id(peer) || endpoints.empty() || out.size() >= limit) {
      return;
   }
   const auto exists = std::ranges::any_of(
       out, [&](const auto& current) { return current.peer == peer && current.discovered_by == source; });
   if (exists) {
      return;
   }
   out.push_back(discovery::result{
       .peer = peer,
       .endpoints = std::move(endpoints),
       // Third-party discovery never establishes peer capabilities or score.
       .discovered_by = source,
       .preferred_path = path::kind::direct,
       .expires_at = expires_at,
   });
}

[[nodiscard]] std::pair<std::shared_ptr<cancellation_latch>, cancellation_latch::subscription>
make_topology_child_cancellation(const std::shared_ptr<cancellation_latch>& parent) {
   auto child = std::make_shared<cancellation_latch>();
   auto subscription = cancellation_latch::subscribe(parent, [child] { child->request_stop(); });
   return {std::move(child), std::move(subscription)};
}

} // namespace

void node::impl::initialize_topology_manager() {
   const auto weak = weak_from_this();
   topology_manager_value = std::make_shared<detail::topology_manager>(
       options.limits.topology,
       detail::topology_manager::callbacks{
           .discover = [weak](std::shared_ptr<cancellation_latch> cancellation)
               -> boost::asio::awaitable<std::vector<discovery::result>> {
              const auto self = weak.lock();
              if (!self) {
                 FORGE_THROW_EXCEPTION(exceptions::closed, "P2P node no longer owns topology discovery");
              }
              co_return co_await self->async_collect_topology_discovery(std::move(cancellation));
           },
           .peer_exchange = [weak](std::shared_ptr<cancellation_latch> cancellation, std::size_t max_parallel_queries)
               -> boost::asio::awaitable<std::vector<discovery::result>> {
              const auto self = weak.lock();
              if (!self) {
                 FORGE_THROW_EXCEPTION(exceptions::closed, "P2P node no longer owns topology peer exchange");
              }
              co_return co_await self->async_collect_topology_peer_exchange(std::move(cancellation),
                                                                            max_parallel_queries);
           },
           .local_rendezvous_record =
               [weak] {
                  if (const auto self = weak.lock()) {
                     return self->topology_rendezvous_local_record();
                  }
                  return detail::topology_manager::callbacks::rendezvous_local_record{};
               },
           .rendezvous_register = [weak](std::size_t point_index, std::string namespace_name,
                                         std::vector<std::uint8_t> signed_peer_record,
                                         std::shared_ptr<cancellation_latch> cancellation)
               -> boost::asio::awaitable<detail::topology_manager::callbacks::rendezvous_register_result> {
              const auto self = weak.lock();
              if (!self) {
                 FORGE_THROW_EXCEPTION(exceptions::closed, "P2P node no longer owns topology rendezvous registration");
              }
              co_return co_await self->async_register_topology_rendezvous(
                  point_index, std::move(namespace_name), std::move(signed_peer_record), std::move(cancellation));
           },
           .rendezvous_discover = [weak](std::size_t point_index, std::string namespace_name, std::size_t limit,
                                         std::vector<std::uint8_t> cookie,
                                         std::shared_ptr<cancellation_latch> cancellation)
               -> boost::asio::awaitable<detail::topology_manager::callbacks::rendezvous_discover_result> {
              const auto self = weak.lock();
              if (!self) {
                 FORGE_THROW_EXCEPTION(exceptions::closed, "P2P node no longer owns topology rendezvous discovery");
              }
              co_return co_await self->async_discover_topology_rendezvous(point_index, std::move(namespace_name), limit,
                                                                          std::move(cookie), std::move(cancellation));
           },
           .rendezvous_unregister = [weak](std::size_t point_index,
                                           std::string namespace_name) -> boost::asio::awaitable<void> {
              if (const auto self = weak.lock()) {
                 co_await self->async_unregister_topology_rendezvous(point_index, std::move(namespace_name));
              }
           },
           .dial = [weak](discovery::result candidate,
                          std::shared_ptr<cancellation_latch> cancellation) -> boost::asio::awaitable<bool> {
              const auto self = weak.lock();
              if (!self) {
                 co_return false;
              }
              co_return co_await self->async_dial_topology_candidate(std::move(candidate), std::move(cancellation));
           },
           .refresh_connection_scores =
               [weak] {
                  if (const auto self = weak.lock()) {
                     self->refresh_connection_scores();
                  }
               },
           .sessions =
               [weak] {
                  if (const auto self = weak.lock()) {
                     return self->topology_sessions();
                  }
                  return connection_manager::snapshot{};
               },
           .plan_peer_prune =
               [weak](std::size_t target, std::size_t maximum, std::chrono::steady_clock::time_point now) {
                  if (const auto self = weak.lock()) {
                     return self->topology_peer_prune_plan(target, maximum, now);
                  }
                  return connection_manager::peer_prune_plan{};
               },
           .close_sessions = [weak](std::vector<std::uint64_t> session_ids) -> boost::asio::awaitable<void> {
              if (const auto self = weak.lock()) {
                 co_await self->async_close_topology_sessions(std::move(session_ids));
              }
           },
       });
}

void node::impl::start_topology_manager() {
   if (topology_manager_value) {
      topology_manager_value->start(lifecycle);
   }
}

boost::asio::awaitable<void> node::impl::async_join_topology_manager() {
   if (topology_manager_value) {
      co_await topology_manager_value->async_join();
   }
}

void node::impl::refresh_connection_scores() {
   auto peers = std::set<peer_id>{};
   {
      const auto lock = std::scoped_lock{mutex};
      for (const auto& session : connections.current(options.limits.max_sessions).sessions) {
         peers.insert(session.peer);
      }
   }

   auto scores = std::vector<std::pair<peer_id, double>>{};
   scores.reserve(peers.size());
   for (const auto& peer : peers) {
      const auto record = store.find(peer);
      scores.emplace_back(peer, record ? record->score : 0.0);
   }

   const auto lock = std::scoped_lock{mutex};
   if (stopped) {
      return;
   }
   for (const auto& [peer, score] : scores) {
      connections.update_network_score(peer, score);
   }
}

connection_manager::snapshot node::impl::topology_sessions() const {
   const auto lock = std::scoped_lock{mutex};
   return connections.current(options.limits.max_sessions);
}

connection_manager::peer_prune_plan node::impl::topology_peer_prune_plan(std::size_t target_peers,
                                                                         std::size_t max_victims,
                                                                         std::chrono::steady_clock::time_point now) {
   const auto lock = std::scoped_lock{mutex};
   return connections.plan_peer_prune(target_peers, max_victims, now);
}

boost::asio::awaitable<void> node::impl::async_close_topology_sessions(std::vector<std::uint64_t> session_ids) {
   auto removed = std::vector<std::shared_ptr<session_state>>{};
   {
      const auto lock = std::scoped_lock{mutex};
      if (stopped) {
         co_return;
      }
      std::sort(session_ids.begin(), session_ids.end());
      session_ids.erase(std::unique(session_ids.begin(), session_ids.end()), session_ids.end());
      removed.reserve(session_ids.size());
      for (const auto id : session_ids) {
         const auto found = sessions.find(id);
         if (found == sessions.end()) {
            continue;
         }
         const auto session = found->second;
         session->closed = true;
         const auto peer = session->info.remote_peer;
         sessions.erase(found);
         connections.forget(id);
         invalidate_pubsub_outbound_locked(peer, id);
         removed.push_back(session);
      }
      for (const auto& session : removed) {
         const auto peer = session->info.remote_peer;
         const auto still_connected = std::ranges::any_of(
             sessions, [&](const auto& item) { return item.second->info.remote_peer == peer && !item.second->closed; });
         if (!still_connected) {
            erase_inbound_relay_reservation_locked(peer);
            forget_pubsub_peer_locked(peer);
         }
      }
      metrics_value.active_sessions = sessions.size();
      metrics_value.sessions_pruned += removed.size();
      metrics_value.sessions_closed += removed.size();
   }

   for (const auto& session : removed) {
      identify_service.forget(session->id);
      session->connection.cancel();
   }
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   for (const auto& session : removed) {
      auto ticket = teardown.track([session] { session->connection.cancel(); });
      if (!ticket.active()) {
         session->resource.release();
         continue;
      }
      try {
         co_await session->connection.async_close();
      } catch (...) {
         session->connection.cancel();
      }
      session->resource.release();
      ticket.release();
   }
}

boost::asio::awaitable<bool>
node::impl::async_dial_topology_candidate(discovery::result candidate,
                                          std::shared_ptr<cancellation_latch> cancellation) {
   if (candidate.peer == local || candidate.endpoints.empty()) {
      co_return false;
   }
   const auto apply_discovery_observation = [&] {
      static_cast<void>(store.apply_discovery(candidate.peer, peer_store::discovery_update{
                                                                  .source = candidate.discovered_by,
                                                                  .observed_at = std::chrono::system_clock::now(),
                                                                  .expires_at = candidate.expires_at,
                                                              }));
   };
   for (auto endpoint : candidate.endpoints) {
      endpoint.peer = candidate.peer;
      if (session_for_path(candidate.peer, path::kind::direct)) {
         apply_discovery_observation();
         co_return true;
      }
      auto session = std::shared_ptr<session_state>{};
      try {
         {
            auto [direct_cancellation, parent_subscription] = make_topology_child_cancellation(cancellation);
            session = co_await connect_direct(endpoint,
                                              node::connect_options{
                                                  .expected_peer = candidate.peer,
                                                  .allow_relay = false,
                                                  .timeout = options.limits.topology.query_timeout,
                                                  .direct_attempt_timeout = options.limits.topology.query_timeout,
                                                  .allow_hole_punch = false,
                                              },
                                              nullptr, std::move(direct_cancellation));
            parent_subscription.reset();
         }
         co_await identify_session(session);
         auto identified = false;
         {
            const auto lock = std::scoped_lock{mutex};
            identified = !session->closed && session->info.identify_state == identify::state::identified;
         }
         if (!identified) {
            co_await async_close_topology_sessions({session->id});
            continue;
         }
         apply_discovery_observation();
         co_return true;
      } catch (const forge::exceptions::base&) {
      }
      if (session) {
         co_await async_close_topology_sessions({session->id});
      }
   }
   co_return false;
}

boost::asio::awaitable<std::vector<discovery::result>>
node::impl::async_collect_topology_discovery(std::shared_ptr<cancellation_latch> cancellation) {
   const auto& policy = options.limits.topology;
   validate_operation_timeout(policy.query_timeout, "P2P discovery refresh timeout");
   const auto now = std::chrono::system_clock::now();
   const auto expires_at = now + policy.refresh_interval;
   const auto query_started = std::chrono::steady_clock::now();
   auto out = std::vector<discovery::result>{};
   out.reserve(policy.max_candidates);

   if (policy.dht_enabled) {
      for (const auto& [protocol, state] : dht_profiles) {
         if (!state->profile.capabilities.peers || out.size() >= policy.max_candidates) {
            continue;
         }
         auto lookup = co_await async_find_dht_peer(
             protocol, local,
             dht::query_options{
                 .requested_count = policy.max_candidates,
                 .quorum = 1,
                 .timeout = remaining_timeout(query_started, policy.query_timeout, "P2P discovery refresh"),
             });
         for (const auto& peer : lookup.closest_peers) {
            if (peer.id == local || out.size() >= policy.max_candidates) {
               continue;
            }
            append_topology_hint(out, peer.id, peer.endpoints, discovery::source::dht, expires_at,
                                 policy.max_candidates);
         }
         (void)remaining_timeout(query_started, policy.query_timeout, "P2P discovery refresh");
      }
   }

   static_cast<void>(cancellation);
   co_return out;
}

detail::topology_manager::callbacks::rendezvous_local_record node::impl::topology_rendezvous_local_record() const {
   const auto snapshot = local_identify_snapshot();
   auto out = detail::topology_manager::callbacks::rendezvous_local_record{.generation = snapshot.generation};
   if (snapshot.document.signed_peer_record.empty()) {
      return out;
   }

   const auto identify_envelope = signed_envelope::decode(snapshot.document.signed_peer_record);
   identify_envelope.verify("libp2p-peer-record", local);
   const auto identify_record = rendezvous::codec::decode_peer_record(identify_envelope.payload);
   if (identify_record.peer != local) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "local Identify peer record peer id mismatch");
   }
   out.signed_peer_record = rendezvous::codec::seal_peer_record(
                                rendezvous::peer_record{
                                    .peer = local,
                                    .endpoints = identify_record.endpoints,
                                    .sequence = identify_record.sequence,
                                },
                                identify_envelope.key, require_libp2p_identity_private_key(identity))
                                .encode();
   return out;
}

boost::asio::awaitable<std::shared_ptr<node::impl::session_state>>
node::impl::ensure_topology_rendezvous_session(std::size_t point_index, bool allow_dial,
                                               std::shared_ptr<cancellation_latch> cancellation) {
   const auto& policy = options.limits.topology;
   const auto point_limit = std::min(policy.rendezvous_points.size(), policy.max_rendezvous_points);
   if (point_index >= point_limit) {
      FORGE_THROW_EXCEPTION(exceptions::internal, "P2P topology rendezvous point index is invalid");
   }
   const auto configured = policy.rendezvous_points[point_index].endpoint;
   if (!configured.peer) {
      FORGE_THROW_EXCEPTION(exceptions::internal, "P2P topology rendezvous point has no peer id");
   }
   const auto rendezvous_peer = *configured.peer;
   if (rendezvous_peer == local) {
      co_return std::shared_ptr<session_state>{};
   }

   auto session = std::shared_ptr<session_state>{};
   const auto configured_key = configured.to_string();
   {
      const auto lock = std::scoped_lock{mutex};
      if (stopped) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "P2P node stopped before topology rendezvous query");
      }
      for (const auto& [_, candidate] : sessions) {
         if (!candidate->closed && candidate->info.remote_peer == rendezvous_peer &&
             candidate->info.path == path::kind::direct && candidate->direct_endpoint &&
             candidate->direct_endpoint->to_string() == configured_key) {
            session = candidate;
            break;
         }
      }
   }
   if (!session) {
      if (!allow_dial) {
         co_return std::shared_ptr<session_state>{};
      }
      store.learn_endpoint(rendezvous_peer, configured);
      {
         auto [direct_cancellation, parent_subscription] = make_topology_child_cancellation(cancellation);
         session = co_await connect_direct(configured,
                                           node::connect_options{
                                               .expected_peer = rendezvous_peer,
                                               .allow_relay = false,
                                               .timeout = policy.query_timeout,
                                               .direct_attempt_timeout = policy.query_timeout,
                                               .allow_hole_punch = false,
                                           },
                                           nullptr, std::move(direct_cancellation));
         parent_subscription.reset();
      }
   }

   if (!allow_dial) {
      const auto lock = std::scoped_lock{mutex};
      if (stopped || session->closed || session->info.identify_state != identify::state::identified ||
          std::ranges::find(session->remote_protocols, builtins::rendezvous) == session->remote_protocols.end()) {
         co_return std::shared_ptr<session_state>{};
      }
      co_return session;
   }

   co_await identify_session(session);
   {
      const auto lock = std::scoped_lock{mutex};
      if (stopped || session->closed) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "P2P topology rendezvous session closed during Identify");
      }
      if (session->info.identify_state != identify::state::identified ||
          std::ranges::find(session->remote_protocols, builtins::rendezvous) == session->remote_protocols.end()) {
         FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol,
                               "configured P2P rendezvous peer does not advertise /rendezvous/1.0.0");
      }
   }
   co_return session;
}

boost::asio::awaitable<rendezvous::message>
node::impl::exchange_topology_rendezvous(const std::shared_ptr<session_state>& session, rendezvous::message request,
                                         std::string_view operation, std::shared_ptr<cancellation_latch> cancellation) {
   const auto started = std::chrono::steady_clock::now();
   const auto timeout = options.limits.topology.query_timeout;
   auto deadline = operation_deadline{runtime.context(), remaining_timeout(started, timeout, operation)};
   auto [operation_cancellation, parent_subscription] = make_topology_child_cancellation(cancellation);
   auto stream_mutex = std::make_shared<std::mutex>();
   auto active_stream = std::make_shared<std::shared_ptr<forge::net::p2p::stream>>();
   operation_cancellation->arm([session, stream_mutex, active_stream] {
      auto stream = std::shared_ptr<forge::net::p2p::stream>{};
      {
         const auto lock = std::scoped_lock{*stream_mutex};
         stream = *active_stream;
      }
      if (stream) {
         stream->cancel();
      } else {
         session->connection.cancel();
      }
   });
   deadline.arm([operation_cancellation] { operation_cancellation->request_stop(); });
   auto stream = std::shared_ptr<forge::net::p2p::stream>{};
   try {
      stream = std::make_shared<forge::net::p2p::stream>(co_await open_session_stream(session, builtins::rendezvous));
      {
         const auto lock = std::scoped_lock{*stream_mutex};
         *active_stream = stream;
      }
      co_await stream->async_write(rendezvous::codec::encode(request, options.limits.rendezvous));
      auto buffer = std::vector<std::uint8_t>{};
      auto response = rendezvous::codec::decode(
          co_await async_read_length_delimited(*stream, buffer, options.limits.rendezvous.max_message_size),
          options.limits.rendezvous);
      co_await stream->async_close();
      if (!deadline.finish()) {
         throw_operation_timeout(operation);
      }
      const auto cancellation_completed = operation_cancellation->finish();
      parent_subscription.reset();
      if (!cancellation_completed) {
         FORGE_THROW_EXCEPTION(exceptions::canceled, "P2P topology rendezvous operation canceled");
      }
      co_return response;
   } catch (...) {
      const auto deadline_completed = deadline.finish();
      const auto cancellation_completed = operation_cancellation->finish();
      parent_subscription.reset();
      if (stream) {
         stream->cancel();
      }
      if (deadline.timed_out() || !deadline_completed) {
         throw_operation_timeout(operation);
      }
      if (!cancellation_completed) {
         const auto lock = std::scoped_lock{mutex};
         if (stopped) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "P2P node stopped during topology rendezvous operation");
         }
         FORGE_THROW_EXCEPTION(exceptions::canceled, "P2P topology rendezvous operation canceled");
      }
      throw;
   }
}

boost::asio::awaitable<detail::topology_manager::callbacks::rendezvous_register_result>
node::impl::async_register_topology_rendezvous(std::size_t point_index, std::string namespace_name,
                                               std::vector<std::uint8_t> signed_peer_record,
                                               std::shared_ptr<cancellation_latch> cancellation) {
   const auto session = co_await ensure_topology_rendezvous_session(point_index, true, cancellation);
   if (!session) {
      co_return detail::topology_manager::callbacks::rendezvous_register_result{};
   }
   const auto response =
       co_await exchange_topology_rendezvous(session,
                                             rendezvous::message{
                                                 .type = rendezvous::message_type::register_peer,
                                                 .register_value =
                                                     rendezvous::register_request{
                                                         .namespace_name = std::move(namespace_name),
                                                         .signed_peer_record = std::move(signed_peer_record),
                                                         .ttl = options.limits.rendezvous.default_ttl,
                                                     },
                                             },
                                             "P2P topology rendezvous registration", std::move(cancellation));
   if (response.type != rendezvous::message_type::register_response || !response.register_response_value) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "rendezvous expected register response");
   }
   co_return detail::topology_manager::callbacks::rendezvous_register_result{
       .accepted = response.register_response_value->status_value == rendezvous::status::ok,
       .ttl = response.register_response_value->ttl,
   };
}

boost::asio::awaitable<detail::topology_manager::callbacks::rendezvous_discover_result>
node::impl::async_discover_topology_rendezvous(std::size_t point_index, std::string namespace_name, std::size_t limit,
                                               std::vector<std::uint8_t> cookie,
                                               std::shared_ptr<cancellation_latch> cancellation) {
   const auto session = co_await ensure_topology_rendezvous_session(point_index, true, cancellation);
   if (!session) {
      co_return detail::topology_manager::callbacks::rendezvous_discover_result{};
   }
   const auto& policy = options.limits.topology;
   const auto response =
       co_await exchange_topology_rendezvous(session,
                                             rendezvous::message{
                                                 .type = rendezvous::message_type::discover,
                                                 .discover_value =
                                                     rendezvous::discover_request{
                                                         .namespace_name = std::move(namespace_name),
                                                         .limit = std::min(limit, policy.max_candidates),
                                                         .cookie = std::move(cookie),
                                                     },
                                             },
                                             "P2P topology rendezvous discovery", std::move(cancellation));
   if (response.type != rendezvous::message_type::discover_response || !response.discover_response_value) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "rendezvous expected discover response");
   }
   if (response.discover_response_value->status_value == rendezvous::status::invalid_cookie) {
      co_return detail::topology_manager::callbacks::rendezvous_discover_result{
          .response_status = detail::topology_manager::callbacks::rendezvous_discover_result::status::invalid_cookie,
      };
   }
   if (response.discover_response_value->status_value != rendezvous::status::ok) {
      co_return detail::topology_manager::callbacks::rendezvous_discover_result{};
   }

   const auto received_at = std::chrono::system_clock::now();
   auto results = std::vector<discovery::result>{};
   results.reserve(std::min(limit, response.discover_response_value->registrations.size()));
   for (auto registration : response.discover_response_value->registrations) {
      registration.expires_at = received_at + registration.ttl;
      auto sanitized = sanitize_discovered_registration(std::move(registration), third_party_discovery_context());
      if (!sanitized) {
         continue;
      }
      if (valid_peer_id(sanitized->peer)) {
         try {
            co_await store.async_upsert_rendezvous(*sanitized);
         } catch (...) {
            co_return detail::topology_manager::callbacks::rendezvous_discover_result{
                .response_status =
                    detail::topology_manager::callbacks::rendezvous_discover_result::status::local_failure,
            };
         }
      }
      if (sanitized->peer == local || results.size() >= limit) {
         continue;
      }
      append_topology_hint(results, sanitized->peer, std::move(sanitized->endpoints), discovery::source::rendezvous,
                           sanitized->expires_at, limit);
   }
   co_return detail::topology_manager::callbacks::rendezvous_discover_result{
       .response_status = detail::topology_manager::callbacks::rendezvous_discover_result::status::ok,
       .results = std::move(results),
       .cookie = response.discover_response_value->cookie,
   };
}

boost::asio::awaitable<void> node::impl::async_unregister_topology_rendezvous(std::size_t point_index,
                                                                              std::string namespace_name) {
   const auto session = co_await ensure_topology_rendezvous_session(point_index, false);
   if (!session) {
      co_return;
   }
   const auto started = std::chrono::steady_clock::now();
   const auto timeout = options.limits.topology.query_timeout;
   auto deadline =
       operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P topology rendezvous unregister")};
   deadline.arm([session] { session->connection.cancel(); });
   try {
      auto stream = co_await open_session_stream(session, builtins::rendezvous);
      co_await stream.async_write(rendezvous::codec::encode(
          rendezvous::message{
              .type = rendezvous::message_type::unregister_peer,
              .unregister_value =
                  rendezvous::unregister_request{
                      .namespace_name = std::move(namespace_name),
                      .peer = local,
                  },
          },
          options.limits.rendezvous));
      co_await stream.async_close();
      if (!deadline.finish()) {
         throw_operation_timeout("P2P topology rendezvous unregister");
      }
   } catch (...) {
      const auto completed = deadline.finish();
      session->connection.cancel();
      if (deadline.timed_out() || !completed) {
         throw_operation_timeout("P2P topology rendezvous unregister");
      }
      throw;
   }
}

boost::asio::awaitable<std::vector<discovery::result>>
node::impl::async_collect_topology_peer_exchange(std::shared_ptr<cancellation_latch> cancellation,
                                                 std::size_t max_parallel_queries) {
   const auto& policy = options.limits.topology;
   if (policy.operating_mode == topology::mode::static_only || !policy.peer_exchange_enabled) {
      co_return std::vector<discovery::result>{};
   }
   if (!cancellation) {
      FORGE_THROW_EXCEPTION(exceptions::internal, "P2P topology peer exchange requires cancellation ownership");
   }

   const auto workers = std::min({max_parallel_queries, policy.max_peer_exchange_peers, policy.max_parallel_queries});
   if (workers == 0) {
      co_return std::vector<discovery::result>{};
   }

   auto parent_cancellation = co_await boost::asio::this_coro::cancellation_state;
   if (parent_cancellation.cancelled() != boost::asio::cancellation_type::none) {
      FORGE_THROW_EXCEPTION(exceptions::canceled, "P2P topology peer exchange canceled before launch");
   }

   const auto executor = co_await boost::asio::this_coro::executor;
   auto claims = std::vector<std::shared_ptr<detail::peer_exchange_scheduler::claim>>{};
   {
      const auto lock = std::scoped_lock{mutex};
      if (stopped || peer_exchange_admission_closed) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "P2P node stopped before topology peer exchange");
      }
      auto selected = peer_exchange_value.claim_batch(peer_exchange_sessions_locked(), std::chrono::steady_clock::now(),
                                                      policy.max_peer_exchange_peers, workers, executor);
      claims.reserve(selected.size());
      for (auto& claim : selected) {
         claims.push_back(std::make_shared<detail::peer_exchange_scheduler::claim>(std::move(claim)));
      }
   }

   if (!claims.empty()) {
      auto batch = std::make_shared<peer_exchange_batch>();
      batch->completed = std::make_shared<detail::lifecycle_wakeup>();
      const auto complete_worker = [batch] {
         auto notify = false;
         {
            const auto lock = std::scoped_lock{batch->mutex};
            if (batch->remaining_workers != 0) {
               --batch->remaining_workers;
            }
            notify = batch->launches_complete && batch->remaining_workers == 0 &&
                     !std::exchange(batch->completion_notified, true);
         }
         if (notify) {
            batch->completed->notify();
         }
      };
      const auto cancel_workers = [batch] {
         auto cancels = std::vector<std::function<void()>>{};
         {
            const auto lock = std::scoped_lock{batch->mutex};
            batch->stop_requested = true;
            cancels = batch->cancel_workers;
         }
         for (const auto& cancel : cancels) {
            cancel();
         }
      };
      auto parent_slot = parent_cancellation.slot();
      if (parent_slot.is_connected()) {
         parent_slot.assign([cancel_workers](boost::asio::cancellation_type) { cancel_workers(); });
      }
      const auto clear_parent_slot = [](boost::asio::cancellation_slot* slot) noexcept { slot->clear(); };
      auto parent_slot_cleanup =
          std::unique_ptr<boost::asio::cancellation_slot, decltype(clear_parent_slot)>{&parent_slot, clear_parent_slot};
      if (parent_cancellation.cancelled() != boost::asio::cancellation_type::none) {
         cancel_workers();
      }
      for (const auto& claim : claims) {
         auto child_signal = std::make_shared<boost::asio::cancellation_signal>();
         const auto cancel_child = [child_signal] { child_signal->emit(boost::asio::cancellation_type::terminal); };
         auto cancel_now = false;
         {
            const auto lock = std::scoped_lock{batch->mutex};
            ++batch->remaining_workers;
            cancel_now = batch->stop_requested;
            if (!cancel_now) {
               batch->cancel_workers.push_back(cancel_child);
            }
         }
         if (cancel_now) {
            cancel_child();
         }
         try {
            auto self = shared_from_this();
            boost::asio::co_spawn(
                executor,
                [self = std::move(self), claim, child_signal]() -> boost::asio::awaitable<void> {
                   static_cast<void>(child_signal);
                   try {
                      co_await self->await_peer_exchange_claim(*claim);
                   } catch (const forge::exceptions::base& error) {
                      auto node_stopped = false;
                      {
                         const auto lock = std::scoped_lock{self->mutex};
                         node_stopped = self->stopped || self->peer_exchange_admission_closed;
                      }
                      if (!node_stopped && p2p_code(error) != exceptions::code::canceled) {
                         forge::exceptions::capture_and_log("P2P topology peer exchange query failed");
                      }
                   } catch (...) {
                      auto node_stopped = false;
                      {
                         const auto lock = std::scoped_lock{self->mutex};
                         node_stopped = self->stopped || self->peer_exchange_admission_closed;
                      }
                      if (!node_stopped) {
                         forge::exceptions::capture_and_log("P2P topology peer exchange query failed");
                      }
                   }
                },
                boost::asio::bind_cancellation_slot(child_signal->slot(),
                                                    [complete_worker](std::exception_ptr) { complete_worker(); }));
         } catch (...) {
            {
               const auto lock = std::scoped_lock{mutex};
               peer_exchange_value.fail(*claim, exceptions::code::closed, "P2P topology peer exchange could not start",
                                        std::chrono::steady_clock::now(), policy.query_timeout);
               peer_exchange_value.leave(*claim);
            }
            complete_worker();
            forge::exceptions::capture_and_log("P2P topology peer exchange query could not launch");
         }
      }

      auto notify = false;
      {
         const auto lock = std::scoped_lock{batch->mutex};
         batch->launches_complete = true;
         notify = batch->remaining_workers == 0 && !std::exchange(batch->completion_notified, true);
      }
      if (notify) {
         batch->completed->notify();
      }

      // The parent slot fans out to every child; the coordinator remains cancelable only after joining them.
      co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
      while (true) {
         const auto observed = batch->completed->epoch();
         {
            const auto lock = std::scoped_lock{batch->mutex};
            if (batch->remaining_workers == 0) {
               break;
            }
         }
         static_cast<void>(co_await batch->completed->async_wait(observed));
      }
   }

   if (parent_cancellation.cancelled() != boost::asio::cancellation_type::none) {
      FORGE_THROW_EXCEPTION(exceptions::canceled, "P2P topology peer exchange canceled");
   }

   auto out = std::vector<discovery::result>{};
   out.reserve(policy.max_candidates);
   const auto fallback_expiry = std::chrono::system_clock::now() + policy.refresh_interval;
   for (const auto& record : store.scored_candidates(discovery::source::peer_exchange, policy.max_candidates)) {
      if (record.peer == local) {
         continue;
      }
      const auto expiry = record.discovery_expires_at == std::chrono::system_clock::time_point{}
                              ? fallback_expiry
                              : record.discovery_expires_at;
      append_topology_result(out, record, discovery::source::peer_exchange, expiry, policy.max_candidates);
   }
   co_return out;
}

} // namespace forge::net::p2p
