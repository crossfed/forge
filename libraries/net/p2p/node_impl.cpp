module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
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

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
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
import forge.crypto.symmetric.chacha20_poly1305;
import forge.crypto.pki.der;
import forge.crypto.asymmetric.ed25519;
import forge.crypto.digest.hmac;
import forge.crypto.asymmetric;
import forge.net.p2p.dht;
import forge.net.p2p.discovery;
import forge.net.p2p.endpoint;
import forge.net.p2p.envelope;
import forge.net.p2p.hole_punch;
import forge.net.p2p.identify;
import forge.net.p2p.lifecycle;
import forge.net.p2p.exceptions;
import forge.net.p2p.message;
import forge.net.p2p.negotiation;
import forge.net.p2p.pubsub;
import forge.net.p2p.reachability;
import forge.net.p2p.rendezvous;
import forge.net.p2p.resource_manager;
import forge.net.p2p.scoring;
import forge.net.p2p.stream;
import forge.net.quic.exceptions;
import forge.crypto.core.random;
import forge.crypto.asymmetric.rsa;
import forge.crypto.digest.sha256;
import forge.crypto.asymmetric.x25519;
import forge.multiformats.types;
import forge.multiformats.varint;
import forge.multiformats.exceptions;
import forge.net.transport.exceptions;
import forge.net.transport.session;
import forge.net.transport.stream;
import forge.net.yamux.exceptions;
import forge.net.yamux.session;

#include "details/node_impl.hxx"
#include "details/peer_exchange_learning.hxx"
#include "details/protocol_capabilities.hxx"

namespace forge::net::p2p {

namespace asio = boost::asio;

[[nodiscard]] exceptions::code map_transport_error(forge::net::transport::exceptions::code kind) noexcept {
   using transport_kind = forge::net::transport::exceptions::code;
   switch (kind) {
   case transport_kind::invalid_endpoint:
      return exceptions::code::invalid_options;
   case transport_kind::closed:
      return exceptions::code::closed;
   case transport_kind::canceled:
      return exceptions::code::canceled;
   case transport_kind::frame_too_large:
   case transport_kind::protocol_error:
   case transport_kind::invalid_buffer:
      return exceptions::code::codec_error;
   case transport_kind::unsupported_protocol:
      return exceptions::code::unsupported_protocol;
   case transport_kind::duplicate_registration:
      return exceptions::code::invalid_options;
   }
   return exceptions::code::internal;
}

[[nodiscard]] exceptions::code map_quic_error(forge::net::quic::exceptions::code kind) noexcept {
   using quic_kind = forge::net::quic::exceptions::code;
   switch (kind) {
   case quic_kind::invalid_endpoint:
   case quic_kind::invalid_options:
      return exceptions::code::invalid_options;
   case quic_kind::connect_timeout:
   case quic_kind::handshake_timeout:
   case quic_kind::idle_timeout:
      return exceptions::code::timeout;
   case quic_kind::tls_failed:
   case quic_kind::peer_verification_failed:
      return exceptions::code::peer_verification_failed;
   case quic_kind::alpn_mismatch:
   case quic_kind::unsupported:
      return exceptions::code::unsupported_protocol;
   case quic_kind::frame_too_large:
   case quic_kind::malformed_frame:
      return exceptions::code::codec_error;
   case quic_kind::backpressure_rejected:
      return exceptions::code::backpressure_rejected;
   case quic_kind::connection_closed:
   case quic_kind::stream_closed:
   case quic_kind::stream_reset:
      return exceptions::code::closed;
   case quic_kind::canceled:
      return exceptions::code::canceled;
   case quic_kind::dependency_unavailable:
   case quic_kind::internal:
      return exceptions::code::internal;
   }
   return exceptions::code::internal;
}

[[nodiscard]] exceptions::code map_yamux_error(forge::net::yamux::exceptions::code kind) noexcept {
   using yamux_kind = forge::net::yamux::exceptions::code;
   switch (kind) {
   case yamux_kind::invalid_options:
      return exceptions::code::invalid_options;
   case yamux_kind::protocol_error:
      return exceptions::code::protocol_error;
   case yamux_kind::resource_limit:
      return exceptions::code::backpressure_rejected;
   case yamux_kind::stream_reset:
   case yamux_kind::closed:
      return exceptions::code::closed;
   case yamux_kind::canceled:
      return exceptions::code::canceled;
   }
   return exceptions::code::internal;
}

[[nodiscard]] exceptions::code p2p_code(const forge::exceptions::base& error) {
   const auto code = exceptions::code_of(error);
   if (code) {
      return *code;
   }
   const auto transport_code = forge::net::transport::exceptions::code_of(error);
   if (transport_code) {
      return map_transport_error(*transport_code);
   }
   if (const auto quic_code = forge::net::quic::exceptions::code_of(error)) {
      return map_quic_error(*quic_code);
   }
   if (const auto yamux_code = forge::net::yamux::exceptions::code_of(error)) {
      return map_yamux_error(*yamux_code);
   }
   return exceptions::code::internal;
}

[[noreturn]] void rethrow_transport_as_p2p(const forge::exceptions::base& error) {
   FORGE_THROW_CODE(p2p_code(error), error.what());
}

[[nodiscard]] bool is_orderly_stream_close(const forge::exceptions::base& error) noexcept {
   return exceptions::is(error, exceptions::code::closed) || exceptions::is(error, exceptions::code::canceled) ||
          forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::closed) ||
          forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::canceled) ||
          forge::net::quic::exceptions::is(error, forge::net::quic::exceptions::code::connection_closed) ||
          forge::net::quic::exceptions::is(error, forge::net::quic::exceptions::code::stream_closed) ||
          forge::net::quic::exceptions::is(error, forge::net::quic::exceptions::code::stream_reset) ||
          forge::net::quic::exceptions::is(error, forge::net::quic::exceptions::code::canceled) ||
          forge::net::yamux::exceptions::is(error, forge::net::yamux::exceptions::code::stream_reset) ||
          forge::net::yamux::exceptions::is(error, forge::net::yamux::exceptions::code::closed) ||
          forge::net::yamux::exceptions::is(error, forge::net::yamux::exceptions::code::canceled);
}

[[nodiscard]] bool is_clean_stream_eof(const forge::exceptions::base& error) noexcept {
   return exceptions::is(error, exceptions::code::closed) ||
          forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::closed) ||
          forge::net::quic::exceptions::is(error, forge::net::quic::exceptions::code::stream_closed) ||
          forge::net::yamux::exceptions::is(error, forge::net::yamux::exceptions::code::closed);
}

[[nodiscard]] std::uint64_t random_nonce() {
   const auto bytes = forge::crypto::core::random_bytes(8);
   auto out = std::uint64_t{};
   for (auto byte : bytes) {
      out = (out << 8U) | byte;
   }
   return out == 0 ? 1 : out;
}

boost::asio::awaitable<std::vector<std::uint8_t>> async_read_length_delimited(forge::net::p2p::stream& stream,
                                                                              std::vector<std::uint8_t>& buffer,
                                                                              std::size_t max_payload_size) {
   while (true) {
      try {
         const auto decoded = forge::multiformats::varint_decode(buffer);
         if (decoded.value > max_payload_size) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "libp2p protobuf message exceeds max size");
         }
         const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
         if (buffer.size() >= total) {
            auto out = std::vector<std::uint8_t>{buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total)};
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total));
            co_return out;
         }
      } catch (const forge::multiformats::exceptions::invalid_format& error) {
         if (std::string_view{error.what()}.find("unterminated") == std::string_view::npos) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, error.what());
         }
      }
      auto chunk = co_await stream.async_read();
      buffer.insert(buffer.end(), chunk.begin(), chunk.end());
   }
}

[[nodiscard]] std::vector<std::uint8_t> wrap_length_delimited(std::span<const std::uint8_t> payload) {
   auto out = forge::multiformats::varint_encode(payload.size());
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> unwrap_length_delimited(std::span<const std::uint8_t> bytes,
                                                                std::size_t max_payload_size) {
   auto decoded = forge::multiformats::decoded_varint{};
   try {
      decoded = forge::multiformats::varint_decode(bytes);
   } catch (const forge::multiformats::exceptions::invalid_format& error) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, error.what());
   }
   if (decoded.value > max_payload_size) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "libp2p protobuf message exceeds max size");
   }
   const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
   if (total != bytes.size()) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "libp2p protobuf message length mismatch");
   }
   return {bytes.begin() + static_cast<std::ptrdiff_t>(decoded.size), bytes.end()};
}

[[nodiscard]] peer_exchange_codec::options codec_for(const node::options& options) noexcept {
   return peer_exchange_codec::options{
       .max_message_size = static_cast<std::uint32_t>(options.limits.max_peer_exchange_message_size),
       .max_endpoint_records = static_cast<std::uint32_t>(options.limits.max_peer_exchange_records),
   };
}

void validate_operation_timeout(std::chrono::milliseconds timeout, std::string_view name) {
   if (timeout.count() <= 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, std::string{name} + " must be positive");
   }
}

[[nodiscard]] std::chrono::milliseconds remaining_timeout(std::chrono::steady_clock::time_point started,
                                                          std::chrono::milliseconds timeout,
                                                          std::string_view operation) {
   validate_operation_timeout(timeout, operation);
   const auto elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
   if (elapsed >= timeout) {
      FORGE_THROW_EXCEPTION(exceptions::timeout, std::string{operation} + " timed out");
   }
   return timeout - elapsed;
}

[[nodiscard]] std::chrono::milliseconds
attempt_timeout(std::chrono::milliseconds remaining, std::chrono::milliseconds configured, std::string_view operation) {
   validate_operation_timeout(remaining, operation);
   validate_operation_timeout(configured, operation);
   return std::min(remaining, configured);
}

[[noreturn]] void throw_operation_timeout(std::string_view operation) {
   FORGE_THROW_EXCEPTION(exceptions::timeout, std::string{operation} + " timed out");
}

resource_manager::limits resource_limits_for(const node::limits& limits) {
   auto value = limits.resources;
   value.max_pending_inbound_sessions = limits.max_pending_inbound_sessions;
   value.max_pending_outbound_sessions = limits.max_pending_outbound_sessions;
   value.max_inbound_sessions = limits.max_inbound_sessions;
   value.max_outbound_sessions = limits.max_outbound_sessions;
   value.max_sessions_per_peer = limits.max_sessions_per_peer;
   return value;
}

void validate(const node::options& options) {
   const auto relay_duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration);
   if (!options.allow_insecure_test_mode && (options.certificate_pem.empty() || options.private_key_pem.empty())) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                            "production P2P node requires mTLS certificate and private key");
   }
   if (options.certificate_pem.empty() != options.private_key_pem.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P certificate and private key must be provided together");
   }
   if (options.explicit_peer_id && !valid_peer_id(*options.explicit_peer_id)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid explicit P2P peer id");
   }
   if (!options.allow_insecure_test_mode && options.explicit_peer_id && !options.certificate_pem.empty() &&
       *options.explicit_peer_id != make_peer_id_from_certificate_pem(options.certificate_pem)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity,
                            "explicit P2P peer id does not match the configured certificate");
   }
   if (options.allow_insecure_test_mode && options.certificate_pem.empty() && !options.explicit_peer_id) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                            "insecure P2P test node without certificate requires explicit peer id");
   }
   if (!options.allow_insecure_test_mode && !options.peer_state.persistence) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "production P2P node requires peer persistence");
   }
   if (options.limits.max_sessions == 0 || options.limits.max_pending_inbound_sessions == 0 ||
       options.limits.max_pending_outbound_sessions == 0 || options.limits.max_inbound_sessions == 0 ||
       options.limits.max_outbound_sessions == 0 || options.limits.max_sessions_per_peer == 0 ||
       options.limits.session_low_watermark == 0 ||
       options.limits.session_low_watermark > options.limits.max_sessions ||
       options.limits.session_grace_period.count() < 0 || options.limits.session_prune_silence.count() <= 0 ||
       options.limits.dial_backoff_base.count() <= 0 || options.limits.dial_backoff_step.count() <= 0 ||
       options.limits.dial_backoff_max.count() <= 0 ||
       options.limits.dial_backoff_base > options.limits.dial_backoff_max ||
       options.limits.max_protocol_handlers == 0 || options.limits.max_peer_exchange_message_size == 0 ||
       options.limits.max_peer_exchange_records == 0 || options.limits.max_peer_exchange_queue == 0 ||
       options.limits.relay.max_active_relays == 0 || options.limits.relay.max_reservations == 0 ||
       options.limits.relay.max_streams_per_reservation == 0 || options.limits.relay.max_relay_bytes == 0 ||
       options.limits.relay.max_queued_bytes == 0 || relay_duration.count() <= 0 ||
       std::chrono::duration_cast<std::chrono::milliseconds>(relay_duration) != options.limits.relay.max_duration ||
       options.limits.relay.reservation_ttl.count() <= 0 || options.limits.resources.max_streams == 0 ||
       options.limits.resources.max_streams_per_peer == 0 || options.limits.resources.max_streams_per_protocol == 0 ||
       options.limits.resources.max_relay_reservations == 0 || options.limits.resources.max_relay_streams == 0 ||
       options.limits.resources.max_queued_bytes == 0 ||
       options.limits.resources.max_dial_attempts == 0 || options.limits.resources.max_dial_attempts_per_peer == 0 ||
       options.limits.resources.max_malformed_messages_per_peer == 0 ||
       options.limits.discovery.query_timeout.count() <= 0 || options.limits.discovery.refresh_interval.count() <= 0 ||
       options.limits.discovery.max_parallel_queries == 0 || options.limits.discovery.max_results == 0 ||
       options.limits.dht.replication == 0 || options.limits.dht.alpha == 0 ||
       options.limits.dht.replacement_cache_size == 0 || options.limits.dht.failure_threshold == 0 ||
       options.limits.dht.max_message_size == 0 || options.limits.dht.max_record_size == 0 ||
       options.limits.dht.max_closer_peers == 0 || options.limits.dht.max_provider_peers == 0 ||
       options.limits.dht.query_timeout.count() <= 0 || options.limits.dht.refresh_interval.count() <= 0 ||
       options.limits.dht.provider_record_ttl.count() <= 0 || options.limits.rendezvous.default_ttl.count() <= 0 ||
       options.limits.rendezvous.min_ttl.count() <= 0 || options.limits.rendezvous.max_ttl.count() <= 0 ||
       options.limits.rendezvous.min_ttl > options.limits.rendezvous.max_ttl ||
       options.limits.rendezvous.max_namespace_size == 0 || options.limits.rendezvous.max_registrations_per_peer == 0 ||
       options.limits.rendezvous.max_discover_limit == 0 || options.limits.rendezvous.max_message_size == 0 ||
       options.limits.pubsub.limits.max_rpc_size == 0 || options.limits.pubsub.limits.max_message_size == 0 ||
       options.limits.pubsub.limits.max_data_size == 0 || options.limits.pubsub.limits.max_topic_size == 0 ||
       options.limits.pubsub.limits.max_subscriptions == 0 || options.limits.pubsub.limits.max_messages == 0 ||
       options.limits.pubsub.limits.max_control_entries == 0 || options.limits.pubsub.limits.max_message_ids == 0 ||
       options.limits.pubsub.limits.max_peers_per_topic == 0 || options.limits.pubsub.limits.max_topics == 0 ||
       options.limits.pubsub.limits.max_validation_queue == 0 ||
       options.limits.pubsub.limits.max_outbound_queue_bytes == 0 ||
       options.limits.pubsub.limits.heartbeat_initial_delay.count() <= 0 ||
       options.limits.pubsub.limits.heartbeat_interval.count() <= 0 ||
       options.limits.pubsub.limits.fanout_ttl.count() <= 0 ||
       options.limits.pubsub.limits.prune_backoff.count() <= 0 ||
       options.limits.pubsub.limits.unsubscribe_backoff.count() <= 0 ||
       options.limits.pubsub.limits.validation_retry_initial_delay.count() <= 0 ||
       options.limits.pubsub.limits.validation_retry_max_delay <
           options.limits.pubsub.limits.validation_retry_initial_delay ||
       options.limits.pubsub.limits.max_validation_attempts == 0 ||
       options.limits.pubsub.limits.max_validation_redeliveries == 0 ||
       options.limits.pubsub.limits.max_validation_requests == 0 || options.limits.pubsub.limits.mesh_n == 0 ||
       options.limits.pubsub.limits.mesh_n_low == 0 ||
       options.limits.pubsub.limits.mesh_n_high < options.limits.pubsub.limits.mesh_n_low ||
       options.limits.pubsub.limits.history_length == 0 || options.limits.pubsub.limits.history_gossip == 0 ||
       options.limits.pubsub.limits.gossip_lazy == 0 || options.limits.pubsub.limits.gossip_factor <= 0.0 ||
       options.limits.pubsub.limits.gossip_retransmission == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid P2P node limits");
   }
   if (!options.path_policy.allow_direct && !options.path_policy.allow_hole_punch && !options.path_policy.allow_relay) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P path policy must allow at least one path kind");
   }
   if (options.path_policy.max_direct_endpoints == 0 || options.path_policy.max_relay_candidates == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P path policy limits must be positive");
   }
   if (options.relay_policy.target_reservations == 0 || options.relay_policy.refresh_margin.count() <= 0 ||
       options.relay_policy.max_candidates_per_refresh == 0 || options.relay_policy.max_parallel_reservations == 0 ||
       options.relay_policy.candidate_backoff.count() <= 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P AutoRelay policy limits must be positive");
   }
   const auto& lifecycle = options.lifecycle;
   if (lifecycle.listen.size() > 1'024 || lifecycle.bootstrap.size() > 4'096 || lifecycle.startup_budget.count() <= 0 ||
       lifecycle.startup_budget > std::chrono::minutes{10} || lifecycle.max_parallel_bootstrap == 0 ||
       lifecycle.max_parallel_bootstrap > 256 || lifecycle.connect_timeout.count() <= 0 ||
       lifecycle.connect_timeout > std::chrono::minutes{5} || lifecycle.bootstrap_retry_initial_delay.count() <= 0 ||
       lifecycle.bootstrap_retry_max_delay < lifecycle.bootstrap_retry_initial_delay ||
       lifecycle.bootstrap_retry_max_delay > std::chrono::hours{1} ||
       !std::isfinite(lifecycle.bootstrap_retry_jitter) || lifecycle.bootstrap_retry_jitter < 0.0 ||
       lifecycle.bootstrap_retry_jitter > 0.20 || lifecycle.maintenance_interval.count() <= 0 ||
       lifecycle.maintenance_interval > std::chrono::minutes{10}) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid P2P node lifecycle options");
   }
   validate_bootstrap(lifecycle.bootstrap, lifecycle.requirement == bootstrap_requirement::require_connection);
   const auto& identify = options.identify;
   if (identify.timeout.count() <= 0 || identify.max_message_size == 0 || identify.max_total_message_size == 0 ||
       identify.max_own_message_size == 0 || identify.max_own_message_size > identify.max_message_size ||
       identify.max_message_parts == 0 ||
       identify.max_message_size > (std::numeric_limits<std::size_t>::max)() / identify.max_message_parts ||
       identify.max_total_message_size < identify.max_message_size ||
       identify.max_total_message_size > identify.max_message_size * identify.max_message_parts ||
       identify.max_protocols == 0 || identify.max_listen_endpoints == 0 || identify.max_protocol_size == 0 ||
       identify.max_version_size == 0 || identify.max_public_key_size == 0 ||
       identify.max_signed_peer_record_size == 0 || identify.max_signed_peer_record_size > identify.max_message_size ||
       identify.max_push_operations == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid P2P Identify limits");
   }
   constexpr auto identify_decode_copies = std::uint64_t{3};
   if (identify.max_total_message_size > (std::numeric_limits<std::uint64_t>::max)() / identify_decode_copies ||
       options.limits.resources.max_queued_bytes <
           static_cast<std::uint64_t>(identify.max_total_message_size) * identify_decode_copies) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                            "P2P queued-byte budget must cover one bounded Identify decode");
   }
}

node::impl::impl(forge::asio::runtime& runtime_value, node::options options_value)
    : runtime(runtime_value), options(std::move(options_value)), identity(make_libp2p_identity_material(options)),
      local(options.explicit_peer_id ? *options.explicit_peer_id
                                     : make_peer_id(decode_public_key(identity.public_key))),
      resources(resource_limits_for(options.limits)), direct_registry(runtime_value, options, identity, resources),
      teardown(runtime_value.context().get_executor()),
      lifecycle(runtime_value.context().get_executor()), identify_service(runtime_value.context().get_executor()),
      store(options.peer_state), routing(local, options.limits.dht) {
   if (!options.allow_insecure_test_mode) {
      const auto identity_peer = make_peer_id(decode_public_key(identity.public_key));
      if (local != identity_peer) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_identity,
                               "configured P2P peer id does not match the identity private key");
      }
      if (!options.certificate_pem.empty() && make_peer_id_from_certificate_pem(options.certificate_pem) != local) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_identity,
                               "configured P2P certificate does not match the identity private key");
      }
   }
}

bool node::impl::launch_tracked(std::function<boost::asio::awaitable<void>()> task) noexcept {
   auto operation = lifecycle.track();
   if (!operation.active()) {
      return false;
   }
   const auto executor = operation.executor();
   const auto cancellation_slot = operation.cancellation_slot();
   const auto stop_latch = operation.stop_latch();
   try {
      asio::co_spawn(
          executor,
          [task = std::move(task), stop_latch]() mutable -> asio::awaitable<void> {
             if (stop_latch && stop_latch->load(std::memory_order_acquire)) {
                co_return;
             }
             co_await task();
          },
          asio::bind_cancellation_slot(cancellation_slot,
                                       [operation = std::move(operation)](std::exception_ptr error) mutable {
                                          static_cast<void>(error);
                                          operation.release();
                                       }));
      return true;
   } catch (...) {
      return false;
   }
}

std::vector<forge::net::p2p::endpoint> node::impl::local_endpoints_for_control() const {
   auto lock = std::scoped_lock{mutex};
   return local_endpoints_for_control_locked();
}

std::vector<forge::net::p2p::endpoint> node::impl::local_endpoints_for_control_locked() const {
   return host_addresses::merge_advertised(options.advertised_endpoints, direct_registry.local_endpoints(), local);
}

[[nodiscard]] std::optional<node::protocol_handler> node::impl::handler_for(const protocol_id& protocol) const {
   auto lock = std::scoped_lock{mutex};
   const auto it = handlers.find(protocol);
   if (it == handlers.end()) {
      return std::nullopt;
   }
   return it->second;
}

[[nodiscard]] std::vector<protocol_id> node::impl::supported_protocols() const {
   auto lock = std::scoped_lock{mutex};
   return supported_protocols_locked();
}

[[nodiscard]] std::vector<protocol_id> node::impl::supported_protocols_locked() const {
   auto out = std::vector<protocol_id>{builtins::ping,
                                       builtins::identify,
                                       builtins::identify_push,
                                       builtins::autonat_v2_dial_request,
                                       builtins::autonat_v2_dial_back,
                                       builtins::autonat_v1,
                                       builtins::relay_stop,
                                       builtins::dcutr};
   if (options.capabilities.has(capabilities::relay) || options.capabilities.has(capabilities::relay_reservation)) {
      out.push_back(builtins::relay_hop);
   }
   if (options.capabilities.has(capabilities::peer_exchange)) {
      out.push_back(builtins::peer_exchange);
   }
   if (options.capabilities.has(capabilities::dht) && options.limits.dht.operating_mode == dht::mode::server) {
      out.push_back(builtins::kad_dht);
   }
   if (options.capabilities.has(capabilities::rendezvous) &&
       (options.limits.rendezvous.operating_role == rendezvous::role::server ||
        options.limits.rendezvous.operating_role == rendezvous::role::client_and_server)) {
      out.push_back(builtins::rendezvous);
   }
   if (options.capabilities.has(capabilities::pubsub)) {
      out.push_back(builtins::meshsub_v11);
      if (options.limits.pubsub.allow_v1_0_fallback) {
         out.push_back(builtins::meshsub_v10);
      }
   }
   out.reserve(out.size() + handlers.size());
   for (const auto& [protocol, _] : handlers) {
      out.push_back(protocol);
   }
   return out;
}

void node::impl::remember_autonat_v2_nonce(const peer_id& peer, std::uint64_t nonce) {
   auto lock = std::scoped_lock{mutex};
   pending_autonat_v2_nonces[peer] = nonce;
}

void node::impl::forget_autonat_v2_nonce(const peer_id& peer) {
   auto lock = std::scoped_lock{mutex};
   pending_autonat_v2_nonces.erase(peer);
}

[[nodiscard]] bool node::impl::consume_autonat_v2_nonce(const peer_id& peer, std::uint64_t nonce) {
   auto lock = std::scoped_lock{mutex};
   const auto it = pending_autonat_v2_nonces.find(peer);
   if (it != pending_autonat_v2_nonces.end() && it->second == nonce) {
      pending_autonat_v2_nonces.erase(it);
      return true;
   }
   if (options.allow_insecure_test_mode) {
      const auto nonce_it =
          std::ranges::find_if(pending_autonat_v2_nonces, [&](const auto& item) { return item.second == nonce; });
      if (nonce_it != pending_autonat_v2_nonces.end()) {
         pending_autonat_v2_nonces.erase(nonce_it);
         return true;
      }
   }
   return false;
}

void node::impl::increment_opened_protocol() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.protocol_streams_opened;
}

void node::impl::increment_protocol_accepted() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.protocol_streams_accepted;
}

void node::impl::increment_protocol_rejected() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.protocol_rejections;
}

void node::impl::increment_peer_exchange() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.peer_exchange_messages;
}

void node::impl::increment_reachability_check(reachability::state state) {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.reachability_checks;
   if (state == reachability::state::publicly_reachable) {
      ++metrics_value.reachability_public;
   } else if (state == reachability::state::private_network || state == reachability::state::blocked ||
              state == reachability::state::relay_only) {
      ++metrics_value.reachability_private;
   }
}

void node::impl::increment_rendezvous_registration() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.rendezvous_registrations;
}

void node::impl::increment_rendezvous_discover() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.rendezvous_discovers;
}

boost::asio::awaitable<void> node::impl::request_peer_exchange(const peer_id& peer) {
   auto session = co_await ensure_direct_session(peer);
   try {
      auto stream = co_await open_session_stream(session, builtins::peer_exchange);
      co_await peer_exchange_codec::async_write(stream,
                                                peer_exchange_message{
                                                    .kind = peer_exchange_message::type::peer_exchange_request,
                                                    .peer = local,
                                                },
                                                codec_for(options));
      auto response = co_await peer_exchange_codec::async_read(stream, codec_for(options));
      if (response.kind != peer_exchange_message::type::peer_exchange_response) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "P2P peer exchange expected response");
      }
      detail::learn_authenticated_peer_exchange_response(store, response, session->info.remote_peer,
                                                         session->remote_endpoint);
      increment_peer_exchange();
      co_await stream.async_close();
   } catch (const forge::exceptions::base& error) {
      rethrow_transport_as_p2p(error);
   }
}

boost::asio::awaitable<void> node::impl::handle_ping(forge::net::p2p::stream stream) {
   try {
      while (true) {
         auto payload = co_await stream.async_read();
         if (payload.size() != 32) {
            FORGE_THROW_EXCEPTION(exceptions::protocol_error, "libp2p ping payload must be 32 bytes");
         }
         co_await stream.async_write(payload);
      }
   } catch (const forge::exceptions::base& error) {
      if (!is_orderly_stream_close(error)) {
         throw;
      }
      co_return;
   } catch (...) {
      throw;
   }
}

boost::asio::awaitable<void> node::impl::handle_autonat_v2_dial_back(std::shared_ptr<node::impl::session_state> session,
                                                                     forge::net::p2p::stream stream) {
   auto buffer = std::vector<std::uint8_t>{};
   auto request = reachability::codec::decode_v2_dial_back(
       co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
   if (request.nonce == 0 || !consume_autonat_v2_nonce(session->info.remote_peer, request.nonce)) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "AutoNAT v2 dial-back nonce mismatch");
   }
   co_await stream.async_write(reachability::codec::encode_v2_dial_back_response(
       reachability::v2::dial_back_response{.status = reachability::v2::dial_back_status::ok}));
   co_await stream.async_close();
}

boost::asio::awaitable<void>
node::impl::handle_autonat_v2_dial_request(std::shared_ptr<node::impl::session_state> session,
                                           forge::net::p2p::stream stream) {
   auto buffer = std::vector<std::uint8_t>{};
   auto request = reachability::codec::decode_v2(
       co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
   auto response = reachability::v2::dial_response{
       .status = reachability::v2::response_status::request_rejected,
       .index = 0,
       .dial_status = reachability::v2::dial_status::unused,
   };
   if (request.type == reachability::v2::message::kind::dial_request && request.dial_request &&
       !request.dial_request->endpoints.empty() && request.dial_request->nonce != 0) {
      response.status = reachability::v2::response_status::dial_refused;
      response.dial_status = reachability::v2::dial_status::dial_error;
      const auto limit = std::min<std::uint64_t>(4096, reachability::options{}.max_data_response_size);
      for (std::size_t index = 0; index < request.dial_request->endpoints.size(); ++index) {
         const auto& candidate = request.dial_request->endpoints[index];
         co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
             .type = reachability::v2::message::kind::dial_data_request,
             .dial_data_request =
                 reachability::v2::dial_data_request{
                     .index = static_cast<std::uint32_t>(index),
                     .bytes = limit,
                 },
         }));
         const auto data = reachability::codec::decode_v2(
             co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
         if (data.type != reachability::v2::message::kind::dial_data_response || !data.dial_data_response ||
             data.dial_data_response->data.size() < limit) {
            response.status = reachability::v2::response_status::request_rejected;
            response.dial_status = reachability::v2::dial_status::dial_error;
            break;
         }
         response.index = static_cast<std::uint32_t>(index);
         try {
            auto dialed = co_await connect_direct(candidate, node::connect_options{
                                                                 .expected_peer = session->info.remote_peer,
                                                                 .allow_relay = false,
                                                                 .timeout = std::chrono::milliseconds{1'500},
                                                             });
            try {
               auto dial_back = co_await open_session_stream(dialed, builtins::autonat_v2_dial_back);
               co_await dial_back.async_write(reachability::codec::encode_v2_dial_back(
                   reachability::v2::dial_back{.nonce = request.dial_request->nonce}));
               auto dial_back_buffer = std::vector<std::uint8_t>{};
               const auto dial_back_response =
                   reachability::codec::decode_v2_dial_back_response(co_await async_read_length_delimited(
                       dial_back, dial_back_buffer, reachability::options{}.max_message_size));
               if (dial_back_response.status == reachability::v2::dial_back_status::ok) {
                  response.status = reachability::v2::response_status::ok;
                  response.dial_status = reachability::v2::dial_status::ok;
                  break;
               }
               response.status = reachability::v2::response_status::ok;
               response.dial_status = reachability::v2::dial_status::dial_back_error;
            } catch (...) {
               response.status = reachability::v2::response_status::ok;
               response.dial_status = reachability::v2::dial_status::dial_back_error;
            }
         } catch (const forge::exceptions::base& error) {
            response.status = reachability::v2::response_status::ok;
            response.dial_status = p2p_code(error) == exceptions::code::peer_verification_failed
                                       ? reachability::v2::dial_status::dial_back_error
                                       : reachability::v2::dial_status::dial_error;
         } catch (...) {
            response.status = reachability::v2::response_status::ok;
            response.dial_status = reachability::v2::dial_status::dial_error;
         }
      }
   }
   co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
       .type = reachability::v2::message::kind::dial_response,
       .dial_response = std::move(response),
   }));
   co_await stream.async_close();
}

boost::asio::awaitable<void> node::impl::handle_autonat_v1(forge::net::p2p::stream stream) {
   auto request = reachability::codec::decode_v1(co_await stream.async_read());
   auto response = reachability::dial_response{
       .status = reachability::dial_status::bad_request,
       .status_text = "expected AutoNAT dial request",
   };
   if (request.kind == reachability::message::message_kind::dial && request.peer && !request.peer->endpoints.empty()) {
      response.status = reachability::dial_status::dial_error;
      response.status_text = "dial failed";
      for (const auto& candidate : request.peer->endpoints) {
         try {
            auto session = co_await connect_direct(candidate, node::connect_options{
                                                                  .expected_peer = request.peer->peer,
                                                                  .allow_relay = false,
                                                                  .timeout = std::chrono::milliseconds{1'500},
                                                              });
            session->closed = true;
            forget_session(session);
            try {
               co_await session->connection.async_close();
            } catch (...) {
               session->connection.cancel();
            }
            response.status = reachability::dial_status::ok;
            response.status_text.clear();
            response.endpoint = candidate;
            break;
         } catch (const forge::exceptions::base& error) {
            response.status = p2p_code(error) == exceptions::code::peer_verification_failed
                                  ? reachability::dial_status::dial_refused
                                  : reachability::dial_status::dial_error;
         } catch (...) {
            response.status = reachability::dial_status::dial_error;
         }
      }
   }
   co_await stream.async_write(reachability::codec::encode_v1(reachability::message{
       .kind = reachability::message::message_kind::dial_response,
       .response = std::move(response),
   }));
   co_await stream.async_close();
}

[[nodiscard]] host_addresses::learning_context
discovery_context_for_session_peer(std::optional<peer_id> session_peer, std::optional<endpoint> session_remote_endpoint,
                                   std::optional<endpoint> session_direct_endpoint, const peer_id& peer) {
   if (session_peer && peer == *session_peer) {
      auto remote_endpoint =
          session_remote_endpoint ? std::move(session_remote_endpoint) : std::move(session_direct_endpoint);
      return host_addresses::learning_context{
          .source = host_addresses::source_kind::authenticated,
          .remote_endpoint = std::move(remote_endpoint),
      };
   }
   return host_addresses::learning_context{.source = host_addresses::source_kind::third_party};
}

namespace {

[[nodiscard]] std::vector<endpoint> endpoints_from_registration(const rendezvous::registration& registration) {
   if (registration.signed_peer_record.empty()) {
      return registration.endpoints;
   }
   try {
      const auto record = rendezvous::codec::open_peer_record(signed_envelope::decode(registration.signed_peer_record),
                                                              registration.peer);
      return record.endpoints;
   } catch (const forge::exceptions::base&) {
      return {};
   }
}

[[nodiscard]] std::optional<rendezvous::registration>
sanitize_registration_for_session(rendezvous::registration registration, const auto& session) {
   const auto original_endpoints = endpoints_from_registration(registration);
   if (original_endpoints.empty()) {
      if (!registration.signed_peer_record.empty()) {
         return std::nullopt;
      }
      return registration;
   }
   auto sanitized = host_addresses::sanitize_discovered_endpoints(
       original_endpoints, registration.peer,
       discovery_context_for_session_peer(session ? std::optional<peer_id>{session->info.remote_peer} : std::nullopt,
                                          session ? session->remote_endpoint : std::nullopt,
                                          session ? session->direct_endpoint : std::nullopt, registration.peer));
   if (sanitized.empty()) {
      return std::nullopt;
   }
   if (registration.signed_peer_record.empty() || sanitized.size() != original_endpoints.size()) {
      registration.signed_peer_record.clear();
   } else {
      for (auto index = std::size_t{0}; index < sanitized.size(); ++index) {
         if (sanitized[index].to_string() != original_endpoints[index].to_string()) {
            registration.signed_peer_record.clear();
            break;
         }
      }
   }
   registration.endpoints = std::move(sanitized);
   return registration;
}

} // namespace

boost::asio::awaitable<void> node::impl::handle_rendezvous(std::shared_ptr<node::impl::session_state> session,
                                                           forge::net::p2p::stream stream) {
   if (!options.capabilities.has(capabilities::rendezvous) ||
       (options.limits.rendezvous.operating_role != rendezvous::role::server &&
        options.limits.rendezvous.operating_role != rendezvous::role::client_and_server)) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "rendezvous server mode is disabled");
   }
   auto buffer = std::vector<std::uint8_t>{};
   auto request = rendezvous::codec::decode(
       co_await async_read_length_delimited(stream, buffer, options.limits.rendezvous.max_message_size),
       options.limits.rendezvous);

   if (request.type == rendezvous::message_type::register_peer && request.register_value) {
      auto response = rendezvous::register_response{.status_value = rendezvous::status::ok};
      auto ttl = request.register_value->ttl.count() == 0 ? options.limits.rendezvous.default_ttl
                                                          : request.register_value->ttl;
      if (ttl < options.limits.rendezvous.min_ttl || ttl > options.limits.rendezvous.max_ttl) {
         response.status_value = rendezvous::status::invalid_ttl;
         response.status_text = "rendezvous registration TTL outside allowed range";
      } else {
         auto endpoints = std::vector<endpoint>{};
         auto registered_peer = session->info.remote_peer;
         if (!request.register_value->signed_peer_record.empty()) {
            try {
               const auto record = rendezvous::codec::open_peer_record(
                   signed_envelope::decode(request.register_value->signed_peer_record), session->info.remote_peer);
               registered_peer = record.peer;
               endpoints = record.endpoints;
            } catch (const forge::exceptions::base&) {
               response.status_value = rendezvous::status::invalid_signed_peer_record;
               response.status_text = "rendezvous signed peer record is invalid";
            }
         } else if (options.limits.rendezvous.require_signed_peer_record) {
            response.status_value = rendezvous::status::invalid_signed_peer_record;
            response.status_text = "rendezvous registration requires signed peer record";
         }
         if (response.status_value == rendezvous::status::ok && endpoints.empty()) {
            if (const auto record = store.find(registered_peer)) {
               for (const auto& endpoint : record->endpoints) {
                  auto item = endpoint.endpoint;
                  item.peer = registered_peer;
                  endpoints.push_back(std::move(item));
               }
            }
         }
         if (response.status_value == rendezvous::status::ok) {
            auto registration = rendezvous::registration{
                .namespace_name = request.register_value->namespace_name,
                .peer = registered_peer,
                .endpoints = std::move(endpoints),
                .signed_peer_record = request.register_value->signed_peer_record,
                .ttl = ttl,
                .expires_at = std::chrono::system_clock::now() + ttl,
            };
            auto sanitized = sanitize_registration_for_session(std::move(registration), session);
            if (!sanitized) {
               response.status_value = rendezvous::status::not_authorized;
               response.status_text = "rendezvous registration endpoints are not routable from source";
            } else {
               try {
                  co_await store.async_register_rendezvous(
                      std::move(*sanitized), options.limits.rendezvous.max_registrations_per_peer);
                  response.ttl = ttl;
                  increment_rendezvous_registration();
               } catch (const forge::exceptions::base& error) {
                  if (exceptions::code_of(error) != exceptions::code::backpressure_rejected) {
                     throw;
                  }
                  response.status_value = rendezvous::status::unavailable;
                  response.status_text = "rendezvous registration capacity reached";
               }
            }
         }
      }
      co_await stream.async_write(rendezvous::codec::encode(
          rendezvous::message{
              .type = rendezvous::message_type::register_response,
              .register_response_value = std::move(response),
          },
          options.limits.rendezvous));
      co_await stream.async_close();
      co_return;
   }

   if (request.type == rendezvous::message_type::unregister_peer && request.unregister_value) {
      co_await store.async_remove_rendezvous(session->info.remote_peer, request.unregister_value->namespace_name);
      co_await stream.async_close();
      co_return;
   }

   if (request.type == rendezvous::message_type::discover && request.discover_value) {
      const auto after = rendezvous::codec::read_cookie(request.discover_value->cookie);
      const auto cookie_namespace = rendezvous::codec::read_cookie_namespace(request.discover_value->cookie);
      if (!request.discover_value->cookie.empty() && cookie_namespace != request.discover_value->namespace_name) {
         co_await stream.async_write(rendezvous::codec::encode(
             rendezvous::message{
                 .type = rendezvous::message_type::discover_response,
                 .discover_response_value =
                     rendezvous::discover_response{
                         .status_value = rendezvous::status::invalid_cookie,
                         .status_text = "rendezvous cookie belongs to a different namespace",
                     },
             },
             options.limits.rendezvous));
         co_await stream.async_close();
         co_return;
      }
      const auto limit = request.discover_value->limit == 0
                             ? options.limits.rendezvous.max_discover_limit
                             : std::min(request.discover_value->limit, options.limits.rendezvous.max_discover_limit);
      auto registrations = store.discover_rendezvous(request.discover_value->namespace_name, after, limit);
      auto sequence = after;
      for (const auto& registration : registrations) {
         sequence = std::max(sequence, registration.sequence);
      }
      increment_rendezvous_discover();
      co_await stream.async_write(rendezvous::codec::encode(
          rendezvous::message{
              .type = rendezvous::message_type::discover_response,
              .discover_response_value =
                  rendezvous::discover_response{
                      .registrations = std::move(registrations),
                      .cookie = rendezvous::codec::make_cookie(sequence, request.discover_value->namespace_name),
                      .status_value = rendezvous::status::ok,
                  },
          },
          options.limits.rendezvous));
      co_await stream.async_close();
      co_return;
   }

   co_await stream.async_write(rendezvous::codec::encode(
       rendezvous::message{
           .type = rendezvous::message_type::discover_response,
           .discover_response_value =
               rendezvous::discover_response{
                   .status_value = rendezvous::status::internal_error,
                   .status_text = "unexpected rendezvous message",
               },
       },
       options.limits.rendezvous));
   co_await stream.async_close();
}

boost::asio::awaitable<void> node::impl::handle_peer_exchange(forge::net::p2p::stream stream,
                                                              std::uint64_t request_id) {
   auto endpoints = std::vector<peer_exchange_message::endpoint_record>{};
   for (const auto& endpoint : local_endpoints_for_control()) {
      endpoints.push_back(peer_exchange_message::endpoint_record{
          .peer = local,
          .endpoint = endpoint,
          .capabilities = options.capabilities,
      });
      if (endpoints.size() >= options.limits.max_peer_exchange_records) {
         break;
      }
   }
   const auto snapshot = store.snapshot(options.limits.max_peer_exchange_records);
   for (const auto& record : snapshot) {
      for (const auto& endpoint : record.endpoints) {
         if (endpoints.size() >= options.limits.max_peer_exchange_records) {
            break;
         }
         endpoints.push_back(peer_exchange_message::endpoint_record{
             .peer = record.peer,
             .endpoint = endpoint.endpoint,
             .capabilities = record.capabilities,
         });
         if (endpoints.size() >= options.limits.max_peer_exchange_records) {
            break;
         }
      }
      if (endpoints.size() >= options.limits.max_peer_exchange_records) {
         break;
      }
   }
   increment_peer_exchange();
   co_await peer_exchange_codec::async_write(stream,
                                             peer_exchange_message{
                                                 .kind = peer_exchange_message::type::peer_exchange_response,
                                                 .request_id = request_id,
                                                 .peer = local,
                                                 .endpoints = std::move(endpoints),
                                             },
                                             codec_for(options));
   co_await stream.async_close();
}

} // namespace forge::net::p2p
