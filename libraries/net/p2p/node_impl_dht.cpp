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

#include "details/dht_exchange.hxx"
#include "details/host_addresses.hxx"
#include "details/node_impl.hxx"
#include "details/operation_deadline.hxx"
#include "details/peer_failure.hxx"

namespace forge::net::p2p {

[[nodiscard]] host_addresses::learning_context
discovery_context_for_session_peer(std::optional<peer_id> session_peer, std::optional<endpoint> session_remote_endpoint,
                                   std::optional<endpoint> session_direct_endpoint, const peer_id& peer);

namespace {

constexpr auto provider_address_ttl = std::chrono::hours{24};

[[nodiscard]] bool protocol_open_failure_requires_peer_penalty(const forge::exceptions::base& error) {
   const auto kind = p2p_code(error);
   return kind == exceptions::code::unsupported_protocol || kind == exceptions::code::protocol_error ||
          kind == exceptions::code::codec_error;
}

void record_dht_exchange_failure(std::mutex& mutex, const bool& stopped, peer_store& store, const peer_id& peer,
                                 const forge::exceptions::base& error) {
   auto node_stopped = false;
   {
      auto lock = std::scoped_lock{mutex};
      node_stopped = stopped;
   }
   if (detail::remote_peer_attributable_failure(p2p_code(error), node_stopped)) {
      store.mark_failure(peer);
   }
}

[[nodiscard]] dht::peer sanitize_discovered_peer_for_session(dht::peer value, const auto& session) {
   value.endpoints = host_addresses::sanitize_discovered_endpoints(
       std::move(value.endpoints), value.id,
       discovery_context_for_session_peer(session ? std::optional<peer_id>{session->info.remote_peer} : std::nullopt,
                                          session ? session->remote_endpoint : std::nullopt,
                                          session ? session->direct_endpoint : std::nullopt, value.id));
   return value;
}

[[nodiscard]] dht::peer provider_peer(const dht::record_store::provider_record& value) {
   return dht::peer{.id = value.provider,
                    .endpoints = value.endpoints,
                    .connection = value.endpoints.empty() ? dht::connection_type::not_connected
                                                          : dht::connection_type::can_connect};
}

[[nodiscard]] std::chrono::system_clock::time_point
value_expiry(const dht::record& value, std::chrono::system_clock::time_point now, const dht::profile& profile) {
   const auto requested = value.ttl.count() > 0 ? value.ttl : profile.limits.provider_record_ttl;
   return now + std::min(requested, profile.limits.provider_record_ttl);
}

void append_unique(std::vector<dht::peer>& peers, dht::peer value, std::size_t limit) {
   const auto current = std::ranges::find_if(peers, [&](const auto& candidate) { return candidate.id == value.id; });
   if (current == peers.end()) {
      if (peers.size() < limit) {
         peers.push_back(std::move(value));
      }
      return;
   }
   for (auto& endpoint : value.endpoints) {
      const auto known = std::ranges::any_of(
          current->endpoints, [&](const auto& candidate) { return candidate.to_string() == endpoint.to_string(); });
      if (!known) {
         current->endpoints.push_back(std::move(endpoint));
      }
   }
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

boost::asio::awaitable<dht::message> node::impl::exchange_dht(const protocol_id& protocol, const peer_id& peer,
                                                              dht::message request, std::chrono::milliseconds timeout) {
   const auto started = std::chrono::steady_clock::now();
   auto& state = dht_profile(protocol);
   auto stream = std::optional<forge::net::p2p::stream>{};
   try {
      stream.emplace(co_await open_protocol_direct(peer, protocol, timeout));
   } catch (const forge::exceptions::base& error) {
      if (protocol_open_failure_requires_peer_penalty(error)) {
         store.mark_failure(peer);
      }
      throw;
   }
   try {
      co_return co_await detail::async_exchange_dht(std::move(*stream), std::move(request), state.profile,
                                                    runtime.context(),
                                                    remaining_timeout(started, timeout, "P2P DHT exchange"));
   } catch (const forge::exceptions::base& error) {
      record_dht_exchange_failure(mutex, stopped, store, peer, error);
      throw;
   }
}

boost::asio::awaitable<void> node::impl::send_dht(const protocol_id& protocol, const peer_id& peer,
                                                  dht::message request, std::chrono::milliseconds timeout) {
   const auto started = std::chrono::steady_clock::now();
   auto& state = dht_profile(protocol);
   auto stream = std::optional<forge::net::p2p::stream>{};
   try {
      stream.emplace(co_await open_protocol_direct(peer, protocol, timeout));
   } catch (const forge::exceptions::base& error) {
      if (protocol_open_failure_requires_peer_penalty(error)) {
         store.mark_failure(peer);
      }
      throw;
   }
   try {
      co_await detail::async_send_dht(std::move(*stream), std::move(request), state.profile, runtime.context(),
                                      remaining_timeout(started, timeout, "P2P DHT send"));
   } catch (const forge::exceptions::base& error) {
      record_dht_exchange_failure(mutex, stopped, store, peer, error);
      throw;
   }
}

boost::asio::awaitable<void> node::impl::handle_dht(std::shared_ptr<node::impl::session_state> session,
                                                    protocol_id protocol, forge::net::p2p::stream stream) {
   auto& state = dht_profile(protocol);
   const auto& profile = state.profile;
   if (profile.operating_mode != dht::mode::server) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "DHT profile server mode is disabled");
   }
   auto buffer = std::vector<std::uint8_t>{};
   while (true) {
      auto deadline = operation_deadline{runtime.context(), profile.limits.query_timeout};
      deadline.arm([&stream] { stream.cancel(); });
      try {
         auto encoded = std::vector<std::uint8_t>{};
         try {
            encoded = co_await async_read_length_delimited(stream, buffer, profile.limits.max_inbound_message_size);
         } catch (const forge::exceptions::base& error) {
            if (!is_clean_stream_eof(error)) {
               throw;
            }
            if (!buffer.empty()) {
               FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT stream ended with a truncated message");
            }
            if (!deadline.finish()) {
               throw_operation_timeout("P2P inbound DHT exchange");
            }
            co_return;
         }

         auto request = dht::codec::decode(encoded, profile);
         increment_dht_query();
         detail::validate_dht_request(request, session->info.remote_peer, profile);
         if (request.type != dht::message_type::ping && request.key_value.bytes.empty()) {
            FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT request key must not be empty");
         }

         auto response = dht::message{.type = request.type, .key_value = request.key_value};
         const auto append_closest = [&] {
            for (auto& peer : state.routing.closest(request.key_value.bytes, profile.limits.replication)) {
               if (peer.id != session->info.remote_peer) {
                  append_unique(response.closer_peers, std::move(peer), profile.limits.replication);
               }
            }
         };

         switch (request.type) {
         case dht::message_type::find_node: {
            try {
               const auto requested = peer_id::from_bytes(request.key_value.bytes);
               if (requested == local) {
                  append_unique(response.closer_peers,
                                dht::peer{.id = local,
                                          .endpoints = local_endpoints_for_control(),
                                          .connection = dht::connection_type::connected},
                                profile.limits.replication);
               } else if (const auto record = store.find(requested)) {
                  auto exact = dht::peer{.id = requested, .connection = dht::connection_type::can_connect};
                  for (const auto& item : record->endpoints) {
                     auto endpoint = item.endpoint;
                     endpoint.peer = requested;
                     exact.endpoints.push_back(std::move(endpoint));
                  }
                  append_unique(response.closer_peers, std::move(exact), profile.limits.replication);
               }
            } catch (const forge::exceptions::base&) {
               // Arbitrary keys use ordinary XOR-distance routing.
            }
            append_closest();
            break;
         }
         case dht::message_type::get_providers:
            for (const auto& provider :
                 state.records.find_providers(request.key_value, profile.limits.max_provider_peers)) {
               if (provider.provider != session->info.remote_peer) {
                  response.provider_peers.push_back(provider_peer(provider));
               }
            }
            append_closest();
            break;
         case dht::message_type::add_provider: {
            const auto now = std::chrono::system_clock::now();
            for (auto provider : request.provider_peers) {
               if (provider.id != session->info.remote_peer || provider.endpoints.empty()) {
                  continue;
               }
               provider = sanitize_discovered_peer_for_session(std::move(provider), session);
               const auto addresses_expire = provider.endpoints.empty() ? now : now + provider_address_ttl;
               co_await state.records.async_upsert_provider(dht::record_store::provider_record{
                   .key = request.key_value,
                   .provider = provider.id,
                   .endpoints = std::move(provider.endpoints),
                   .provider_expires_at = now + profile.limits.provider_record_ttl,
                   .addresses_expires_at = addresses_expire,
               });
            }
            if (!deadline.finish()) {
               throw_operation_timeout("P2P inbound DHT exchange");
            }
            continue;
         }
         case dht::message_type::put_value: {
            const auto now = std::chrono::system_clock::now();
            const auto stored = co_await state.records.async_put(
                dht::record_store::value_record{
                    .record = *request.record_value,
                    .expires_at = value_expiry(*request.record_value, now, profile),
                },
                now);
            if (stored.outcome != dht::record_store::put_outcome::incoming_stored) {
               FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT PUT_VALUE incoming record was not selected");
            }
            response.record_value = *request.record_value;
            break;
         }
         case dht::message_type::get_value:
            if (const auto value = state.records.find_value(request.key_value)) {
               response.record_value = value->record;
            }
            append_closest();
            break;
         case dht::message_type::ping:
            break;
         }

         increment_dht_response();
         co_await stream.async_write(dht::codec::encode(response, profile));
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
}

} // namespace forge::net::p2p
