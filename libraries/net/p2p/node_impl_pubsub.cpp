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
#include <functional>
#include <iterator>
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
import forge.net.p2p.discovery;
import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;
import forge.net.p2p.negotiation;
import forge.net.p2p.pubsub;
import forge.net.p2p.resource_manager;
import forge.net.p2p.stream;
import forge.net.transport.stream;
import forge.net.yamux.session;

#include "details/node_impl.hxx"
#include "details/peer_failure.hxx"

namespace forge::net::p2p {

namespace asio = boost::asio;

[[nodiscard]] exceptions::code p2p_code(const forge::exceptions::base& error);
[[nodiscard]] bool is_orderly_stream_close(const forge::exceptions::base& error) noexcept;

boost::asio::awaitable<std::vector<std::uint8_t>> async_read_length_delimited(forge::net::p2p::stream& stream,
                                                                              std::vector<std::uint8_t>& buffer,
                                                                              std::size_t max_payload_size);

void node::impl::invalidate_pubsub_outbound_locked(const peer_id& peer, std::optional<std::uint64_t> owner_session_id) {
   const auto found = pubsub_value.outbound.find(peer);
   if (found == pubsub_value.outbound.end() || (owner_session_id && found->second.session_id != *owner_session_id)) {
      return;
   }
   found->second.write_gate->close();
   pubsub_value.outbound.erase(found);
}

void node::impl::clear_pubsub_outbound_locked() {
   for (const auto& [_, generation] : pubsub_value.outbound) {
      generation.write_gate->close();
   }
   pubsub_value.outbound.clear();
   pubsub_value.connection_gates.close();
   pubsub_value.outbound_budget.clear();
}

void node::impl::reserve_pubsub_outbound_bytes(const peer_id& peer, std::size_t bytes) {
   auto lock = std::scoped_lock{mutex};
   if (stopped) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "cannot publish GossipSub RPC after node shutdown");
   }
   const auto limit = options.limits.pubsub.limits.max_outbound_queue_bytes;
   if (!pubsub_value.outbound_budget.reserve(peer, bytes, limit)) {
      ++metrics_value.backpressure_rejections;
      ++metrics_value.protocol_rejections;
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "GossipSub outbound queue byte limit reached");
   }
}

void node::impl::release_pubsub_outbound_bytes(const peer_id& peer, std::size_t bytes) noexcept {
   auto lock = std::scoped_lock{mutex};
   pubsub_value.outbound_budget.release(peer, bytes);
}

[[nodiscard]] std::string bytes_key(std::span<const std::uint8_t> bytes) {
   return {bytes.begin(), bytes.end()};
}

[[nodiscard]] bool same_pubsub_message(const pubsub::message& left, const pubsub::message& right) noexcept {
   return left.from == right.from && left.data == right.data && left.seqno == right.seqno &&
          left.subject == right.subject;
}

[[nodiscard]] std::chrono::milliseconds validation_retry_delay(const pubsub::limits& limits, std::size_t ordinal) {
   auto delay = limits.validation_retry_initial_delay;
   for (auto attempt = std::size_t{1}; attempt < ordinal && delay < limits.validation_retry_max_delay; ++attempt) {
      if (delay > limits.validation_retry_max_delay / 2) {
         return limits.validation_retry_max_delay;
      }
      delay *= 2;
   }
   return std::min(delay, limits.validation_retry_max_delay);
}

[[nodiscard]] std::vector<std::uint8_t> uint64_be(std::uint64_t value) {
   auto out = std::vector<std::uint8_t>(8);
   for (auto i = std::size_t{}; i < out.size(); ++i) {
      out[out.size() - 1 - i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU);
   }
   return out;
}

void node::impl::increment_pubsub_published() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_messages_published;
}

void node::impl::increment_pubsub_received() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_messages_received;
}

void node::impl::increment_pubsub_delivered() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_messages_delivered;
}

void node::impl::increment_pubsub_duplicate() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_duplicates;
}

void node::impl::increment_pubsub_invalid(const peer_id& peer) {
   auto offender = std::shared_ptr<session_state>{};
   auto endpoint = std::optional<forge::net::p2p::endpoint>{};
   {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.pubsub_invalid_messages;
      pubsub_value.scores[peer].invalid_messages += 1;
      pubsub_value.scores[peer].value -= 1.0;
      if (!resources.record_malformed(resource_manager::scope{.peer = peer, .protocol = builtins::meshsub_v11})) {
         ++metrics_value.connection_rejections;
         for (const auto& [_, session] : sessions) {
            if (session->info.remote_peer == peer && !session->closed) {
               offender = session;
            }
         }
         if (offender) {
            endpoint = offender->direct_endpoint;
         }
      }
   }
   if (offender) {
      if (endpoint) {
         store.mark_endpoint_failure(peer, *endpoint, path::kind::direct,
                                     endpoint_backoff_until(peer, *endpoint, path::kind::direct));
      }
      forget_session(offender);
      offender->connection.cancel();
   }
}

void node::impl::increment_pubsub_control() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_control_messages;
}

std::vector<std::uint8_t> node::impl::next_pubsub_seqno() {
   auto lock = std::scoped_lock{mutex};
   return uint64_be(pubsub_value.next_seqno++);
}

pubsub::snapshot node::impl::pubsub_snapshot() const {
   auto lock = std::scoped_lock{mutex};
   auto mesh_edges = std::size_t{};
   for (const auto& [_, peers] : pubsub_value.mesh) {
      mesh_edges += peers.size();
   }
   return pubsub::snapshot{
       .topics = pubsub_value.handlers.size(),
       .peers = pubsub_value.peer_topics.size(),
       .mesh_edges = mesh_edges,
       .cached_messages = pubsub_value.cache.size(),
       .messages_published = metrics_value.pubsub_messages_published,
       .messages_received = metrics_value.pubsub_messages_received,
       .messages_delivered = metrics_value.pubsub_messages_delivered,
       .duplicates = metrics_value.pubsub_duplicates,
       .invalid_messages = metrics_value.pubsub_invalid_messages,
       .control_messages = metrics_value.pubsub_control_messages,
   };
}

std::vector<pubsub::subscription> node::impl::local_pubsub_subscriptions() const {
   auto lock = std::scoped_lock{mutex};
   auto out = std::vector<pubsub::subscription>{};
   out.reserve(pubsub_value.handlers.size());
   for (const auto& [topic_value, _] : pubsub_value.handlers) {
      out.push_back(pubsub::subscription{.subscribe = true, .subject = pubsub::topic{.value = topic_value}});
   }
   return out;
}

std::vector<peer_id> node::impl::pubsub_candidate_peers(const std::string& topic_value,
                                                        std::optional<peer_id> except) const {
   auto out = std::vector<peer_id>{};
   {
      auto lock = std::scoped_lock{mutex};
      if (const auto mesh = pubsub_value.mesh.find(topic_value); mesh != pubsub_value.mesh.end()) {
         for (const auto& peer : mesh->second) {
            if (!except || peer != *except) {
               out.push_back(peer);
            }
         }
      }
      for (const auto& [peer, topics] : pubsub_value.peer_topics) {
         if (topics.contains(topic_value) && (!except || peer != *except) &&
             std::ranges::find(out, peer) == out.end()) {
            out.push_back(peer);
         }
      }
      for (const auto& [_, session] : sessions) {
         const auto& peer = session->info.remote_peer;
         if ((!except || peer != *except) && std::ranges::find(out, peer) == out.end()) {
            out.push_back(peer);
         }
      }
   }
   for (const auto& record : store.candidates(capabilities::pubsub, options.peer_state.max_peers)) {
      const auto supports_pubsub = record.capabilities.has(capabilities::pubsub) ||
                                   std::ranges::any_of(record.protocols, [](const protocol_id& protocol) {
                                      return protocol == builtins::meshsub_v11 || protocol == builtins::meshsub_v10;
                                   });
      if (supports_pubsub && (!except || record.peer != *except) && std::ranges::find(out, record.peer) == out.end()) {
         out.push_back(record.peer);
      }
   }
   return out;
}

boost::asio::awaitable<std::shared_ptr<node::impl::session_state>>
node::impl::ensure_pubsub_direct_session(const peer_id& peer) {
   auto participant = detail::connection_singleflight_registry::lease{};
   auto start = std::optional<detail::connection_singleflight_registry::operation>{};
   auto tracked = detail::session_teardown::ticket{};
   auto existing = std::shared_ptr<session_state>{};
   {
      auto lock = std::scoped_lock{mutex};
      if (stopped) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "cannot connect GossipSub peer after node shutdown");
      }
      existing = session_for_path_locked(peer, path::kind::direct, std::nullopt);
      if (!existing) {
         auto joined = pubsub_value.connection_gates.join(peer, runtime.context().get_executor());
         participant = std::move(joined.participant);
         start = std::move(joined.start);
         if (start) {
            tracked = teardown.track();
         }
      }
   }
   if (existing) {
      co_return existing;
   }
   auto release_participant = [this, &participant](void*) noexcept {
      auto lock = std::scoped_lock{mutex};
      pubsub_value.connection_gates.leave(participant);
   };
   auto participant_guard = std::unique_ptr<void, decltype(release_participant)>{this, std::move(release_participant)};

   if (start) {
      auto self = shared_from_this();
      auto active = std::make_shared<detail::connection_singleflight_registry::operation>(std::move(*start));
      auto tracked_operation = std::make_shared<detail::session_teardown::ticket>(std::move(tracked));
      if (!launch_tracked([self, peer, active, tracked_operation]() mutable -> boost::asio::awaitable<void> {
             static_cast<void>(tracked_operation);
             try {
                static_cast<void>(
                    co_await self->ensure_direct_session(peer, self->options.limits.discovery.query_timeout));
                auto lock = std::scoped_lock{self->mutex};
                self->pubsub_value.connection_gates.succeed(*active);
             } catch (const forge::exceptions::base& error) {
                auto lock = std::scoped_lock{self->mutex};
                self->pubsub_value.connection_gates.fail(*active, p2p_code(error), error.what());
             } catch (...) {
                auto lock = std::scoped_lock{self->mutex};
                self->pubsub_value.connection_gates.fail(*active, exceptions::code::internal,
                                                         "GossipSub peer connection failed internally");
             }
             co_return;
          })) {
         auto lock = std::scoped_lock{mutex};
         pubsub_value.connection_gates.fail(*active, exceptions::code::internal,
                                            "GossipSub peer connection could not be started");
      }
   }

   auto result = detail::connection_singleflight_registry::outcome{};
   try {
      result = co_await participant.wait();
   } catch (const boost::system::system_error& error) {
      if (error.code() == boost::asio::error::operation_aborted) {
         FORGE_THROW_EXCEPTION(exceptions::canceled, "GossipSub peer connection canceled while waiting");
      }
      FORGE_THROW_EXCEPTION(exceptions::internal, "GossipSub peer connection wait failed",
                            forge::exceptions::ctx("reason", error.code().message()));
   }
   if (!result.succeeded) {
      FORGE_THROW_CODE(result.error.value_or(exceptions::code::internal), std::move(result.message));
   }
   if (auto connected = session_for_path(peer, path::kind::direct)) {
      co_return connected;
   }
   FORGE_THROW_EXCEPTION(exceptions::closed, "GossipSub direct session closed after connection singleflight");
}

boost::asio::awaitable<void> node::impl::send_pubsub_rpc(const peer_id& peer, const pubsub::rpc& value) {
   auto protocol = builtins::meshsub_v11;
   if (options.limits.pubsub.allow_v1_0_fallback) {
      const auto record = store.find(peer);
      const auto supports_v11 = record && std::ranges::any_of(record->protocols, [](const protocol_id& value) {
                                   return value == builtins::meshsub_v11;
                                });
      const auto supports_v10 = record && std::ranges::any_of(record->protocols, [](const protocol_id& value) {
                                   return value == builtins::meshsub_v10;
                                });
      if (supports_v10 && !supports_v11) {
         protocol = builtins::meshsub_v10;
      }
   }
   const auto encoded = pubsub::codec::encode(value, options.limits.pubsub);
   reserve_pubsub_outbound_bytes(peer, encoded.size());
   auto release_bytes = [this, peer, bytes = encoded.size()](void*) noexcept {
      release_pubsub_outbound_bytes(peer, bytes);
   };
   auto reservation = std::unique_ptr<void, decltype(release_bytes)>{this, std::move(release_bytes)};
   try {
      // Connection singleflight is acquired before the write gate. A synchronous subscription announce can
      // therefore reuse the remembered session without re-entering this publication generation.
      auto session = co_await ensure_pubsub_direct_session(peer);
      const auto session_id = session->id;
      auto write_gate = std::shared_ptr<forge::asio::gate>{};
      {
         auto lock = std::scoped_lock{mutex};
         const auto current_session = sessions.find(session_id);
         if (stopped || current_session == sessions.end() || current_session->second != session || session->closed) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "GossipSub direct session closed before publication");
         }
         auto current = pubsub_value.outbound.find(peer);
         if (current == pubsub_value.outbound.end() || current->second.session_id != session_id ||
             !current->second.write_gate || current->second.write_gate->closed()) {
            if (current != pubsub_value.outbound.end()) {
               current->second.write_gate->close();
            }
            pubsub_value.outbound[peer] = pubsub_state::outbound_generation{
                .session_id = session_id,
                .write_gate = std::make_shared<forge::asio::gate>(),
            };
         }
         write_gate = pubsub_value.outbound.at(peer).write_gate;
      }
      auto write_ticket = forge::asio::gate::ticket{};
      try {
         write_ticket = co_await write_gate->acquire();
      } catch (const forge::asio::exceptions::canceled&) {
         FORGE_THROW_EXCEPTION(exceptions::canceled, "GossipSub publication canceled while waiting for peer stream");
      } catch (const forge::asio::exceptions::rejected&) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "GossipSub peer stream closed while waiting for publication");
      }
      auto outbound = std::shared_ptr<forge::net::p2p::stream>{};
      {
         auto lock = std::scoped_lock{mutex};
         const auto current = pubsub_value.outbound.find(peer);
         if (stopped || current == pubsub_value.outbound.end() || current->second.session_id != session_id ||
             current->second.write_gate != write_gate) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "GossipSub peer stream was closed before publication");
         }
         if (current->second.stream && current->second.stream->valid()) {
            outbound = current->second.stream;
         }
      }
      if (!outbound) {
         const auto open_timeout =
             attempt_timeout(options.limits.discovery.query_timeout, node::open_options{}.direct_attempt_timeout,
                             "GossipSub protocol open direct attempt");
         auto stream = co_await open_protocol_on_direct_session(peer, protocol, session, open_timeout);
         outbound = std::make_shared<forge::net::p2p::stream>(std::move(stream));
         auto stale = false;
         {
            auto lock = std::scoped_lock{mutex};
            const auto current = pubsub_value.outbound.find(peer);
            stale = stopped || current == pubsub_value.outbound.end() || current->second.session_id != session_id ||
                    current->second.write_gate != write_gate;
            if (!stale) {
               current->second.stream = outbound;
            }
         }
         if (stale) {
            outbound->cancel();
            FORGE_THROW_EXCEPTION(exceptions::closed, "GossipSub peer stream closed while opening publication stream");
         }
      }
      try {
         co_await outbound->async_write(encoded);
      } catch (const forge::exceptions::base&) {
         auto lock = std::scoped_lock{mutex};
         const auto current = pubsub_value.outbound.find(peer);
         if (current != pubsub_value.outbound.end() && current->second.session_id == session_id &&
             current->second.stream == outbound) {
            invalidate_pubsub_outbound_locked(peer, session_id);
         }
         throw;
      }
   } catch (...) {
      reservation.reset();
      throw;
   }
   reservation.reset();
}

void node::impl::record_pubsub_send_failure(const peer_id& peer, const forge::exceptions::base& error) {
   const auto kind = p2p_code(error);
   auto node_stopped = false;
   {
      auto lock = std::scoped_lock{mutex};
      node_stopped = stopped;
   }
   if (!detail::remote_peer_attributable_failure(kind, node_stopped)) {
      return;
   }
   store.mark_failure(peer);
   routing.mark_failure(peer);
}

boost::asio::awaitable<void> node::impl::announce_pubsub_subscriptions(const peer_id& peer) {
   if (!options.capabilities.has(capabilities::pubsub)) {
      co_return;
   }
   auto subscriptions = local_pubsub_subscriptions();
   if (subscriptions.empty()) {
      co_return;
   }
   try {
      co_await send_pubsub_rpc(peer, pubsub::rpc{.subscriptions = std::move(subscriptions)});
   } catch (const forge::exceptions::base& error) {
      record_pubsub_send_failure(peer, error);
   }
}

void node::impl::finish_pubsub_validation(const peer_id& peer) {
   auto lock = std::scoped_lock{mutex};
   if (pubsub_value.active_validations > 0) {
      --pubsub_value.active_validations;
   }
   if (auto it = pubsub_value.active_validations_by_peer.find(peer);
       it != pubsub_value.active_validations_by_peer.end()) {
      if (it->second > 1) {
         --it->second;
      } else {
         pubsub_value.active_validations_by_peer.erase(it);
      }
   }
}

node::impl::pubsub_state::claim node::impl::claim_pubsub_message(const peer_id& peer, const std::string& key,
                                                                 const pubsub::message& value,
                                                                 bool requires_validation) {
   auto lock = std::scoped_lock{mutex};
   const auto begin_validation = [&](pubsub_state::validation& validation) {
      if (!requires_validation) {
         validation.state = pubsub_state::validation::status::accepted;
         return pubsub_state::claim_status::claimed;
      }
      if (pubsub_value.active_validations >= options.limits.pubsub.limits.max_validation_queue) {
         ++metrics_value.backpressure_rejections;
         const auto ordinal = std::max(validation.attempts, validation.redeliveries + 1);
         const auto retry_after =
             std::chrono::steady_clock::now() + validation_retry_delay(options.limits.pubsub.limits, ordinal);
         validation.state = pubsub_state::validation::status::retryable;
         validation.retry_after = retry_after;
         validation.request_after = retry_after;
         return pubsub_state::claim_status::backpressured;
      }
      validation.state = pubsub_state::validation::status::in_progress;
      ++validation.attempts;
      validation.requests = 0;
      ++pubsub_value.active_validations;
      ++pubsub_value.active_validations_by_peer[peer];
      return pubsub_state::claim_status::claimed;
   };

   const auto cached = pubsub_value.cache.find(key);
   if (cached == pubsub_value.cache.end()) {
      const auto generation = pubsub_value.next_validation_generation++;
      pubsub_value.cache.emplace(key, value);
      pubsub_value.history.push_back(key);
      const auto validation = pubsub_value.validations
                                  .emplace(key,
                                           pubsub_state::validation{
                                               .source = peer,
                                               .generation = generation,
                                           })
                                  .first;
      const auto status = begin_validation(validation->second);
      prune_pubsub_cache_locked();
      return pubsub_state::claim{
          .status = status,
          .generation = generation,
      };
   }

   const auto validation = pubsub_value.validations.find(key);
   if (!same_pubsub_message(cached->second, value)) {
      return pubsub_state::claim{.status = pubsub_state::claim_status::invalid};
   }
   if (validation != pubsub_value.validations.end() &&
       validation->second.state == pubsub_state::validation::status::retryable &&
       std::chrono::steady_clock::now() >= validation->second.retry_after) {
      if (validation->second.redeliveries >= options.limits.pubsub.limits.max_validation_redeliveries) {
         validation->second.state = pubsub_state::validation::status::ignored;
         validation->second.retry_after = {};
         validation->second.request_after = {};
         return pubsub_state::claim{.status = pubsub_state::claim_status::duplicate};
      }
      ++validation->second.redeliveries;
      validation->second.state = pubsub_state::validation::status::claimed;
      validation->second.source = peer;
      validation->second.retry_after = {};
      validation->second.request_after = {};
      const auto status = begin_validation(validation->second);
      prune_pubsub_cache_locked();
      return pubsub_state::claim{
          .status = status,
          .generation = validation->second.generation,
      };
   }

   ++pubsub_value.scores[peer].duplicate_messages;
   ++metrics_value.pubsub_duplicates;
   return pubsub_state::claim{.status = pubsub_state::claim_status::duplicate};
}

bool node::impl::complete_pubsub_message(const std::string& key, std::uint64_t generation,
                                         pubsub::validation_result result) {
   auto lock = std::scoped_lock{mutex};
   const auto found = pubsub_value.validations.find(key);
   if (found == pubsub_value.validations.end() || found->second.generation != generation) {
      return false;
   }
   switch (result) {
   case pubsub::validation_result::accept:
      found->second.state = pubsub_state::validation::status::accepted;
      found->second.retry_after = {};
      found->second.request_after = {};
      found->second.requests = 0;
      break;
   case pubsub::validation_result::reject:
      found->second.state = pubsub_state::validation::status::rejected;
      found->second.retry_after = {};
      found->second.request_after = {};
      found->second.requests = 0;
      break;
   case pubsub::validation_result::ignore:
      found->second.state = pubsub_state::validation::status::ignored;
      found->second.retry_after = {};
      found->second.request_after = {};
      found->second.requests = 0;
      break;
   case pubsub::validation_result::retry:
      return false;
   }
   prune_pubsub_cache_locked();
   return true;
}

void node::impl::defer_pubsub_message(const std::string& key, std::uint64_t generation) {
   auto lock = std::scoped_lock{mutex};
   const auto found = pubsub_value.validations.find(key);
   if (found == pubsub_value.validations.end() || found->second.generation != generation) {
      return;
   }
   if (found->second.attempts >= options.limits.pubsub.limits.max_validation_attempts ||
       found->second.redeliveries >= options.limits.pubsub.limits.max_validation_redeliveries) {
      found->second.state = pubsub_state::validation::status::ignored;
      found->second.retry_after = {};
      found->second.request_after = {};
      found->second.requests = 0;
      prune_pubsub_cache_locked();
      return;
   }

   const auto ordinal = std::max(found->second.attempts, found->second.redeliveries + 1);
   const auto retry_after =
       std::chrono::steady_clock::now() + validation_retry_delay(options.limits.pubsub.limits, ordinal);
   found->second.state = pubsub_state::validation::status::retryable;
   found->second.retry_after = retry_after;
   found->second.request_after = retry_after;
   prune_pubsub_cache_locked();
}

bool node::impl::should_request_pubsub_message_locked(const std::string& key, const peer_id& source,
                                                      std::chrono::steady_clock::time_point now) {
   if (!pubsub_value.cache.contains(key)) {
      return true;
   }
   const auto validation = pubsub_value.validations.find(key);
   if (validation == pubsub_value.validations.end() ||
       validation->second.state != pubsub_state::validation::status::retryable || validation->second.source != source ||
       now < validation->second.retry_after || now < validation->second.request_after) {
      return false;
   }
   if (validation->second.requests >= options.limits.pubsub.limits.max_validation_requests) {
      validation->second.state = pubsub_state::validation::status::ignored;
      validation->second.retry_after = {};
      validation->second.request_after = {};
      return false;
   }

   ++validation->second.requests;
   auto delay = options.limits.pubsub.limits.validation_retry_initial_delay;
   const auto maximum = options.limits.pubsub.limits.validation_retry_max_delay;
   for (auto request = std::size_t{1}; request < validation->second.requests && delay < maximum; ++request) {
      if (delay > maximum / 2) {
         delay = maximum;
      } else {
         delay *= 2;
      }
   }
   validation->second.request_after =
       now + std::max(options.limits.pubsub.limits.heartbeat_interval, std::min(delay, maximum));
   return true;
}

bool node::impl::can_serve_pubsub_message_locked(const std::string& key) const {
   const auto validation = pubsub_value.validations.find(key);
   return validation != pubsub_value.validations.end() &&
          validation->second.state == pubsub_state::validation::status::accepted;
}

void node::impl::remember_local_pubsub_message_locked(const std::string& key, pubsub::message value) {
   if (!pubsub_value.cache.contains(key)) {
      pubsub_value.history.push_back(key);
   }
   pubsub_value.cache[key] = std::move(value);
   pubsub_value.validations[key] = pubsub_state::validation{
       .state = pubsub_state::validation::status::accepted,
       .generation = pubsub_value.next_validation_generation++,
   };
   prune_pubsub_cache_locked();
}

void node::impl::prune_pubsub_cache_locked() {
   const auto max_cached = std::max<std::size_t>(
       options.limits.pubsub.limits.history_length * options.limits.pubsub.limits.max_messages, 1);
   while (pubsub_value.history.size() > max_cached) {
      auto evicted = false;
      for (auto history = pubsub_value.history.begin(); history != pubsub_value.history.end(); ++history) {
         const auto validation = pubsub_value.validations.find(*history);
         if (validation != pubsub_value.validations.end() &&
             validation->second.state == pubsub_state::validation::status::in_progress) {
            continue;
         }
         pubsub_value.cache.erase(*history);
         pubsub_value.validations.erase(*history);
         pubsub_value.history.erase(history);
         evicted = true;
         break;
      }
      if (!evicted) {
         break;
      }
   }
}

bool node::impl::pubsub_control_over_limit(const pubsub::control& value) const noexcept {
   return value.have.size() > options.limits.pubsub.limits.max_ihave_per_peer ||
          value.want.size() > options.limits.pubsub.limits.max_iwant_per_peer ||
          value.grafts.size() > options.limits.pubsub.limits.max_graft_per_peer;
}

void node::impl::launch_pubsub_heartbeat() {
   if (!options.capabilities.has(capabilities::pubsub)) {
      return;
   }
   {
      auto lock = std::scoped_lock{mutex};
      if (pubsub_value.heartbeat_started) {
         return;
      }
      pubsub_value.heartbeat_started = true;
   }
   auto self = shared_from_this();
   if (!launch_tracked([self]() -> asio::awaitable<void> {
          auto timer = asio::steady_timer{co_await asio::this_coro::executor};
          timer.expires_after(self->options.limits.pubsub.limits.heartbeat_initial_delay);
          boost::system::error_code ec;
          co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
          while (true) {
             {
                auto lock = std::scoped_lock{self->mutex};
                if (self->stopped) {
                   co_return;
                }
             }
             co_await self->pubsub_heartbeat_once();
             timer.expires_after(self->options.limits.pubsub.limits.heartbeat_interval);
             ec = {};
             co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
          }
       })) {
      auto lock = std::scoped_lock{mutex};
      pubsub_value.heartbeat_started = false;
   }
}

boost::asio::awaitable<void> node::impl::pubsub_heartbeat_once() {
   auto grafts = std::map<peer_id, std::vector<pubsub::control::graft>>{};
   auto prunes = std::map<peer_id, std::vector<pubsub::control::prune>>{};
   auto gossip = std::map<peer_id, std::vector<pubsub::control::ihave>>{};
   auto retries = std::map<peer_id, std::vector<std::vector<std::uint8_t>>>{};
   {
      auto lock = std::scoped_lock{mutex};
      if (stopped) {
         co_return;
      }
      const auto now = std::chrono::steady_clock::now();
      const auto mesh_high =
          std::min(options.limits.pubsub.limits.mesh_n_high, options.limits.pubsub.limits.max_peers_per_topic);
      const auto mesh_target = std::min(options.limits.pubsub.limits.mesh_n, mesh_high);
      for (const auto& [topic_value, _] : pubsub_value.handlers) {
         auto& mesh = pubsub_value.mesh[topic_value];
         for (auto it = mesh.begin(); it != mesh.end();) {
            const auto has_session = std::ranges::any_of(sessions, [&](const auto& item) {
               return item.second->info.remote_peer == *it && !item.second->closed;
            });
            const auto topics = pubsub_value.peer_topics.find(*it);
            const auto subscribed = topics != pubsub_value.peer_topics.end() && topics->second.contains(topic_value);
            if (!has_session && !subscribed) {
               it = mesh.erase(it);
            } else {
               ++it;
            }
         }
         for (const auto& [peer, topics] : pubsub_value.peer_topics) {
            if (mesh.size() >= mesh_target) {
               break;
            }
            if (topics.contains(topic_value) && !mesh.contains(peer)) {
               mesh.insert(peer);
               grafts[peer].push_back(pubsub::control::graft{.subject = pubsub::topic{.value = topic_value}});
            }
         }
         for (const auto& [_, session] : sessions) {
            if (mesh.size() >= mesh_target) {
               break;
            }
            const auto& peer = session->info.remote_peer;
            if (!mesh.contains(peer)) {
               mesh.insert(peer);
               grafts[peer].push_back(pubsub::control::graft{.subject = pubsub::topic{.value = topic_value}});
            }
         }
         while (mesh.size() > mesh_high) {
            auto it = std::prev(mesh.end());
            const auto peer = *it;
            mesh.erase(it);
            prunes[peer].push_back(pubsub::control::prune{
                .subject = pubsub::topic{.value = topic_value},
                .backoff = options.limits.pubsub.limits.prune_backoff,
            });
         }
         auto ids = std::vector<std::vector<std::uint8_t>>{};
         auto seen = std::size_t{};
         for (auto it = pubsub_value.history.rbegin();
              it != pubsub_value.history.rend() && seen < options.limits.pubsub.limits.history_gossip; ++it, ++seen) {
            if (const auto found = pubsub_value.cache.find(*it); found != pubsub_value.cache.end() &&
                                                                 found->second.subject.value == topic_value &&
                                                                 can_serve_pubsub_message_locked(*it)) {
               ids.push_back(pubsub::codec::message_id(found->second));
            }
         }
         if (!ids.empty()) {
            if (ids.size() > options.limits.pubsub.limits.gossip_lazy) {
               ids.resize(options.limits.pubsub.limits.gossip_lazy);
            }
            for (const auto& peer : mesh) {
               gossip[peer].push_back(pubsub::control::ihave{
                   .subject = pubsub::topic{.value = topic_value},
                   .message_ids = ids,
               });
            }
         }
      }
      auto retry_count = std::size_t{};
      auto retry = pubsub_value.retry_cursor.empty() ? pubsub_value.validations.begin()
                                                     : pubsub_value.validations.upper_bound(pubsub_value.retry_cursor);
      const auto retry_budget =
          std::min(options.limits.pubsub.limits.gossip_lazy, options.limits.pubsub.limits.max_messages);
      for (auto inspected = std::size_t{}; inspected < pubsub_value.validations.size() && retry_count < retry_budget;
           ++inspected) {
         if (retry == pubsub_value.validations.end()) {
            retry = pubsub_value.validations.begin();
         }
         auto& [key, validation] = *retry;
         pubsub_value.retry_cursor = key;
         ++retry;
         if (validation.state != pubsub_state::validation::status::retryable || validation.source.value.empty() ||
             now < validation.retry_after || now < validation.request_after) {
            continue;
         }
         const auto cached = pubsub_value.cache.find(key);
         if (cached == pubsub_value.cache.end()) {
            continue;
         }
         const auto source = validation.source;
         const auto existing = retries.find(source);
         if (existing != retries.end() && existing->second.size() >= options.limits.pubsub.limits.max_message_ids) {
            continue;
         }
         if (!should_request_pubsub_message_locked(key, source, now)) {
            continue;
         }
         retries[source].push_back(pubsub::codec::message_id(cached->second));
         ++retry_count;
      }
   }
   for (const auto& [peer, items] : grafts) {
      try {
         co_await send_pubsub_rpc(peer, pubsub::rpc{.control_value = pubsub::control{.grafts = items}});
      } catch (const forge::exceptions::base& error) {
         record_pubsub_send_failure(peer, error);
      }
   }
   for (const auto& [peer, items] : prunes) {
      try {
         co_await send_pubsub_rpc(peer, pubsub::rpc{.control_value = pubsub::control{.prunes = items}});
      } catch (const forge::exceptions::base& error) {
         record_pubsub_send_failure(peer, error);
      }
   }
   for (const auto& [peer, items] : gossip) {
      try {
         co_await send_pubsub_rpc(peer, pubsub::rpc{.control_value = pubsub::control{.have = items}});
      } catch (const forge::exceptions::base& error) {
         record_pubsub_send_failure(peer, error);
      }
   }
   for (const auto& [peer, message_ids] : retries) {
      try {
         co_await send_pubsub_rpc(peer, pubsub::rpc{.control_value = pubsub::control{
                                                        .want = std::vector<pubsub::control::iwant>{
                                                            pubsub::control::iwant{.message_ids = message_ids}}}});
      } catch (const forge::exceptions::base& error) {
         record_pubsub_send_failure(peer, error);
      }
   }
}

boost::asio::awaitable<void> node::impl::handle_pubsub(std::shared_ptr<node::impl::session_state> session,
                                                       forge::net::p2p::stream stream) {
   if (!options.capabilities.has(capabilities::pubsub)) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "GossipSub is disabled");
   }

   auto buffer = std::vector<std::uint8_t>{};
   while (true) {
      auto payload = std::vector<std::uint8_t>{};
      auto close_after_error = false;
      try {
         payload = co_await async_read_length_delimited(stream, buffer, options.limits.pubsub.limits.max_rpc_size);
      } catch (const forge::exceptions::base& error) {
         auto closed_by_node = false;
         {
            auto lock = std::scoped_lock{mutex};
            closed_by_node = stopped || session->closed;
         }
         if (closed_by_node || is_orderly_stream_close(error)) {
            co_return;
         }
         increment_pubsub_invalid(session->info.remote_peer);
         increment_protocol_rejected();
         close_after_error = true;
      }
      if (close_after_error) {
         co_await stream.async_close();
         co_return;
      }

      auto value = pubsub::rpc{};
      close_after_error = false;
      try {
         value = pubsub::codec::decode(payload, options.limits.pubsub);
      } catch (const forge::exceptions::base&) {
         increment_pubsub_invalid(session->info.remote_peer);
         increment_protocol_rejected();
         close_after_error = true;
      }
      if (close_after_error) {
         co_await stream.async_close();
         co_return;
      }

      if (!value.subscriptions.empty()) {
         auto announce_back = std::vector<pubsub::subscription>{};
         auto subscription_limit_reached = false;
         {
            auto lock = std::scoped_lock{mutex};
            for (const auto& subscription : value.subscriptions) {
               if (subscription.subscribe) {
                  auto& mesh = pubsub_value.mesh[subscription.subject.value];
                  if (!mesh.contains(session->info.remote_peer) &&
                      mesh.size() >= options.limits.pubsub.limits.max_peers_per_topic) {
                     subscription_limit_reached = true;
                     continue;
                  }
                  pubsub_value.peer_topics[session->info.remote_peer].insert(subscription.subject.value);
                  mesh.insert(session->info.remote_peer);
                  if (pubsub_value.handlers.contains(subscription.subject.value)) {
                     announce_back.push_back(pubsub::subscription{
                         .subscribe = true,
                         .subject = subscription.subject,
                     });
                  }
               } else if (auto topics = pubsub_value.peer_topics.find(session->info.remote_peer);
                          topics != pubsub_value.peer_topics.end()) {
                  topics->second.erase(subscription.subject.value);
                  if (auto mesh = pubsub_value.mesh.find(subscription.subject.value); mesh != pubsub_value.mesh.end()) {
                     mesh->second.erase(session->info.remote_peer);
                  }
               }
            }
         }
         if (subscription_limit_reached) {
            increment_pubsub_invalid(session->info.remote_peer);
            increment_protocol_rejected();
         }
         if (!announce_back.empty()) {
            co_await send_pubsub_rpc(session->info.remote_peer, pubsub::rpc{.subscriptions = std::move(announce_back)});
         }
      }

      if (value.control_value) {
         increment_pubsub_control();
         if (pubsub_control_over_limit(*value.control_value)) {
            increment_pubsub_invalid(session->info.remote_peer);
            increment_protocol_rejected();
            co_await stream.async_close();
            co_return;
         }
         auto missing = std::vector<std::vector<std::uint8_t>>{};
         auto cached = std::vector<pubsub::message>{};
         {
            auto lock = std::scoped_lock{mutex};
            for (const auto& graft : value.control_value->grafts) {
               if (pubsub_value.handlers.contains(graft.subject.value)) {
                  pubsub_value.mesh[graft.subject.value].insert(session->info.remote_peer);
               }
            }
            for (const auto& prune : value.control_value->prunes) {
               if (auto mesh = pubsub_value.mesh.find(prune.subject.value); mesh != pubsub_value.mesh.end()) {
                  mesh->second.erase(session->info.remote_peer);
               }
            }
            for (const auto& ihave : value.control_value->have) {
               if (!pubsub_value.handlers.contains(ihave.subject.value)) {
                  continue;
               }
               const auto now = std::chrono::steady_clock::now();
               for (const auto& id : ihave.message_ids) {
                  const auto key = bytes_key(id);
                  const auto cached_message = pubsub_value.cache.find(key);
                  if (cached_message != pubsub_value.cache.end() && cached_message->second.subject != ihave.subject) {
                     continue;
                  }
                  if (should_request_pubsub_message_locked(key, session->info.remote_peer, now)) {
                     missing.push_back(id);
                  }
               }
            }
            for (const auto& iwant : value.control_value->want) {
               for (const auto& id : iwant.message_ids) {
                  const auto key = bytes_key(id);
                  if (const auto found = pubsub_value.cache.find(key);
                      found != pubsub_value.cache.end() && can_serve_pubsub_message_locked(key)) {
                     cached.push_back(found->second);
                  }
               }
            }
         }
         if (!missing.empty()) {
            try {
               co_await send_pubsub_rpc(
                   session->info.remote_peer,
                   pubsub::rpc{.control_value =
                                   pubsub::control{.want = std::vector<pubsub::control::iwant>{
                                                       pubsub::control::iwant{.message_ids = std::move(missing)}}}});
            } catch (const forge::exceptions::base&) {
               increment_protocol_rejected();
            }
         }
         if (!cached.empty()) {
            try {
               co_await send_pubsub_rpc(session->info.remote_peer, pubsub::rpc{.messages = std::move(cached)});
            } catch (const forge::exceptions::base&) {
               increment_protocol_rejected();
            }
         }
      }

      for (const auto& published : value.messages) {
         increment_pubsub_received();

         auto signature_ok = true;
         const auto signed_message = !published.signature.empty();
         switch (options.limits.pubsub.signatures) {
         case pubsub::signature_policy::strict_sign:
            signature_ok = pubsub::codec::verify_message(published);
            break;
         case pubsub::signature_policy::strict_no_sign:
            signature_ok = !signed_message;
            break;
         case pubsub::signature_policy::lax_sign:
         case pubsub::signature_policy::lax_no_sign:
            signature_ok = !signed_message || pubsub::codec::verify_message(published);
            break;
         }
         if (!signature_ok) {
            increment_pubsub_invalid(session->info.remote_peer);
            continue;
         }

         const auto id = pubsub::codec::message_id(published);
         const auto key = bytes_key(id);
         auto handler = std::optional<pubsub::handler>{};
         {
            auto lock = std::scoped_lock{mutex};
            if (const auto found = pubsub_value.handlers.find(published.subject.value);
                found != pubsub_value.handlers.end()) {
               handler = found->second;
            }
         }
         const auto claim = claim_pubsub_message(session->info.remote_peer, key, published, handler.has_value());
         if (claim.status == pubsub_state::claim_status::invalid) {
            increment_pubsub_invalid(session->info.remote_peer);
            increment_protocol_rejected();
            continue;
         }
         if (claim.status == pubsub_state::claim_status::duplicate ||
             claim.status == pubsub_state::claim_status::backpressured) {
            continue;
         }

         auto result = pubsub::validation_result::accept;
         if (handler) {
            try {
               result = co_await (*handler)(pubsub::event{
                   .source = session->info.remote_peer,
                   .value = published,
               });
               finish_pubsub_validation(session->info.remote_peer);
            } catch (...) {
               finish_pubsub_validation(session->info.remote_peer);
               defer_pubsub_message(key, claim.generation);
               throw;
            }
         }
         if (result == pubsub::validation_result::retry) {
            defer_pubsub_message(key, claim.generation);
            continue;
         }
         if (handler && !complete_pubsub_message(key, claim.generation, result)) {
            continue;
         }
         if (result == pubsub::validation_result::reject) {
            increment_pubsub_invalid(session->info.remote_peer);
            continue;
         }
         if (result == pubsub::validation_result::accept && handler) {
            increment_pubsub_delivered();
         }

         auto should_forward = false;
         {
            auto lock = std::scoped_lock{mutex};
            should_forward = pubsub_value.handlers.contains(published.subject.value) ||
                             pubsub_value.mesh.contains(published.subject.value);
         }
         if (!should_forward) {
            continue;
         }
         for (const auto& peer : pubsub_candidate_peers(published.subject.value, session->info.remote_peer)) {
            try {
               co_await send_pubsub_rpc(peer, pubsub::rpc{.messages = std::vector<pubsub::message>{published}});
            } catch (const forge::exceptions::base&) {
               increment_protocol_rejected();
            }
         }
      }
   }
}

} // namespace forge::net::p2p
