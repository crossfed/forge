module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module forge.net.p2p.node;

import forge.asio.runtime;
import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.resource_manager;
import forge.multiformats.exceptions;
import forge.multiformats.types;
import forge.multiformats.varint;
import forge.multiformats.multicodec;
import forge.multiformats.multihash;
import forge.multiformats.multibase;
import forge.multiformats.multiaddr;
import forge.net.quic.connection;
import forge.net.quic.connector;
import forge.net.quic.endpoint;
import forge.net.quic.exceptions;
import forge.net.quic.listener;
import forge.net.quic.options;
import forge.net.quic.security;
import forge.net.quic.transport;
import forge.net.transport.limits;
import forge.net.transport.session;

#include "details/direct_transport.hxx"
#include "details/cancellation_latch.hxx"

namespace forge::net::p2p::direct {
namespace {

[[nodiscard]] forge::net::quic::transport_limits quic_limits(const forge::net::transport::limits& value) noexcept {
   return forge::net::quic::transport_limits{
       .max_connections = value.max_connections,
       .max_streams_per_connection = value.max_streams_per_connection,
       .max_queued_bytes = value.max_queued_bytes,
       .max_inbound_queued_bytes = value.max_inbound_queued_bytes,
       .max_inbound_queued_packets = value.max_inbound_queued_packets,
       .max_frame_size = value.max_frame_size,
   };
}

[[nodiscard]] forge::net::quic::endpoint quic_endpoint_for(const forge::net::p2p::endpoint& value) {
   if (!value.is_direct_quic()) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct QUIC endpoint");
   }
   return forge::net::quic::from_transport_endpoint(value.transport);
}

[[nodiscard]] forge::net::p2p::endpoint p2p_endpoint_for(const forge::net::quic::endpoint& value) {
   return forge::net::p2p::endpoint{.transport = forge::net::quic::to_transport_endpoint(value)};
}

[[nodiscard]] std::string listener_key(forge::net::p2p::endpoint value) {
   value.peer.reset();
   return value.to_string();
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
   case quic_kind::peer_verification_failed:
   case quic_kind::alpn_mismatch:
   case quic_kind::tls_failed:
      return exceptions::code::peer_verification_failed;
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
   case quic_kind::unsupported:
      return exceptions::code::internal;
   }
   return exceptions::code::internal;
}

[[noreturn]] void rethrow_quic_as_p2p(const forge::exceptions::base& error) {
   const auto code = forge::net::quic::exceptions::code_of(error);
   if (code) {
      FORGE_THROW_CODE(map_quic_error(*code), error.what());
   }
   throw;
}

[[nodiscard]] peer_id insecure_legacy_peer_id(std::span<const std::uint8_t> der) {
   return peer_id::from_bytes(forge::multiformats::multihash::sha2_256(der).encode());
}

[[nodiscard]] peer_id strict_peer_id_from_certificate_der(std::span<const std::uint8_t> der) {
   try {
      return make_peer_id_from_certificate_der(der);
   } catch (const forge::exceptions::base&) {
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed,
                            "P2P peer certificate is missing a valid signed libp2p identity extension");
   }
}

[[nodiscard]] peer_id verified_peer_id_for(const forge::net::quic::connection& connection,
                                           const std::optional<peer_id>& expected, bool insecure_test_mode) {
   if (insecure_test_mode) {
      if (expected) {
         return *expected;
      }
      if (const auto certificate = connection.peer_certificate()) {
         try {
            return make_peer_id_from_certificate_der(certificate->der);
         } catch (const forge::exceptions::base&) {
            // Insecure test mode still accepts legacy certificates without the libp2p extension.
         }
         return insecure_legacy_peer_id(certificate->der);
      }
      return peer_id{.value = "insecure-test-peer"};
   }

   const auto certificate = connection.peer_certificate();
   if (!certificate) {
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed, "P2P session has no verified peer certificate");
   }
   const auto remote = strict_peer_id_from_certificate_der(certificate->der);
   if (expected && remote != *expected) {
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed, "P2P peer id does not match expected peer");
   }
   return remote;
}

[[nodiscard]] std::optional<peer_id> expected_peer_for(const forge::net::p2p::endpoint& endpoint,
                                                       const node::connect_options& options) {
   if (options.expected_peer) {
      return options.expected_peer;
   }
   return endpoint.peer;
}

class quic_profile final {
   struct listener_entry {
      std::shared_ptr<forge::net::quic::listener> value;
      forge::net::p2p::endpoint local;
      bool active = true;
   };

 public:
   quic_profile(forge::asio::runtime& runtime_value, const node::options& options_value,
                resource_manager resources_value)
       : runtime_(runtime_value), options_(options_value), resources_(std::move(resources_value)) {}

   [[nodiscard]] bool supports(const forge::net::p2p::endpoint& endpoint) const noexcept {
      return endpoint.is_direct_quic();
   }

   [[nodiscard]] bool listening() const noexcept {
      auto lock = std::scoped_lock{listeners_mutex_};
      return std::ranges::any_of(listeners_, [](const auto& item) { return item.second.active; });
   }

   [[nodiscard]] std::vector<forge::net::p2p::endpoint> local_endpoints() const {
      auto out = std::vector<forge::net::p2p::endpoint>{};
      auto lock = std::scoped_lock{listeners_mutex_};
      out.reserve(listeners_.size());
      for (const auto& [_, listener] : listeners_) {
         if (listener.active) {
            out.push_back(listener.local);
         }
      }
      return out;
   }

   forge::net::p2p::endpoint listen(forge::net::p2p::endpoint endpoint) {
      if (!endpoint.is_direct_quic()) {
         FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct QUIC endpoint");
      }
      const auto requested_key = listener_key(endpoint);
      {
         auto lock = std::scoped_lock{listeners_mutex_};
         if (listeners_stopped_) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "P2P QUIC direct listener is stopped");
         }
         if (endpoint.transport.port != 0) {
            auto found = listeners_.find(requested_key);
            if (found != listeners_.end() && found->second.active) {
               FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                                     "P2P QUIC direct listener endpoint is already active");
            }
         }
      }
      try {
         auto listener =
             std::make_shared<forge::net::quic::listener>(runtime_, quic_endpoint_for(endpoint), server_options());
         auto local = p2p_endpoint_for(listener->local_endpoint());
         const auto key = listener_key(local);
         auto stopped = false;
         auto duplicate = false;
         {
            auto lock = std::scoped_lock{listeners_mutex_};
            stopped = listeners_stopped_;
            const auto found = listeners_.find(key);
            duplicate = found != listeners_.end() && found->second.active;
            if (!stopped && !duplicate) {
               listeners_.emplace(key, listener_entry{.value = listener, .local = local, .active = true});
            }
         }
         if (stopped || duplicate) {
            try {
               listener->stop();
            } catch (...) {
            }
         }
         if (stopped) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "P2P QUIC direct listener is stopped");
         }
         if (duplicate) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P QUIC direct listener endpoint is already active");
         }
         return local;
      } catch (const forge::exceptions::base& error) {
         rethrow_quic_as_p2p(error);
      }
   }

   void stop() {
      auto listeners = stop_listeners();
      for (const auto& listener : listeners) {
         try {
            listener->stop();
         } catch (...) {
         }
      }
      auto active = std::vector<std::shared_ptr<cancellation_latch>>{};
      {
         auto lock = std::scoped_lock{active_mutex_};
         stopped_ = true;
         for (auto iterator = active_.begin(); iterator != active_.end();) {
            if (auto operation = iterator->lock()) {
               active.push_back(std::move(operation));
               ++iterator;
            } else {
               iterator = active_.erase(iterator);
            }
         }
      }
      for (const auto& operation : active) {
         operation->request_stop();
      }
   }

   boost::asio::awaitable<void> async_stop() {
      stop();
      auto listeners = listener_snapshot();
      for (const auto& listener : listeners) {
         co_await listener->async_stop();
      }
   }

   boost::asio::awaitable<connection> async_connect(forge::net::p2p::endpoint endpoint,
                                                    const node::connect_options& options,
                                                    std::shared_ptr<cancellation_latch> cancellation) {
      auto cancel_current = cancellation ? std::move(cancellation) : std::make_shared<cancellation_latch>();
      track(cancel_current);
      auto connector = std::make_shared<forge::net::quic::connector>(runtime_);
      cancel_current->arm([connector] { connector->cancel(); });
      try {
         const auto expected_peer = expected_peer_for(endpoint, options);
         auto quic = co_await connector->async_connect(quic_endpoint_for(endpoint),
                                                       client_options(expected_peer, options.timeout));
         const auto remote = verified_peer_id_for(quic, expected_peer, options_.allow_insecure_test_mode);
         auto local_endpoint = p2p_endpoint_for(quic.local_endpoint());
         auto remote_endpoint = p2p_endpoint_for(quic.remote_endpoint());
         if (!cancel_current->finish()) {
            quic.cancel();
            FORGE_THROW_EXCEPTION(exceptions::canceled, "P2P QUIC direct connect canceled");
         }
         co_return connection{
             .peer = remote,
             .session = forge::net::quic::as_transport_session(std::move(quic)),
             .local_endpoint = std::move(local_endpoint),
             .remote_endpoint = std::move(remote_endpoint),
         };
      } catch (const forge::exceptions::base& error) {
         static_cast<void>(cancel_current->finish());
         rethrow_quic_as_p2p(error);
      } catch (...) {
         static_cast<void>(cancel_current->finish());
         throw;
      }
   }

   boost::asio::awaitable<connection> async_accept(forge::net::p2p::endpoint endpoint) {
      try {
         const auto key = listener_key(std::move(endpoint));
         auto listener = std::shared_ptr<forge::net::quic::listener>{};
         {
            auto lock = std::scoped_lock{listeners_mutex_};
            const auto found = listeners_.find(key);
            if (found != listeners_.end() && found->second.active) {
               listener = found->second.value;
            }
         }
         if (!listener) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "P2P QUIC direct listener is not active");
         }
         auto quic = co_await listener->async_accept();
         if (!listener_is_current(key, listener)) {
            try {
               quic.cancel();
            } catch (...) {
            }
            FORGE_THROW_EXCEPTION(exceptions::closed, "P2P QUIC direct listener stopped during accept");
         }
         auto admission_token = forge::net::quic::detail::connection_access::take_inbound_admission(quic);
         auto admission = std::static_pointer_cast<resource_manager::session_reservation>(std::move(admission_token));
         if (!admission || !admission->active()) {
            quic.cancel();
            FORGE_THROW_EXCEPTION(exceptions::internal, "P2P QUIC connection is missing inbound admission");
         }
         const auto remote = verified_peer_id_for(quic, std::nullopt, options_.allow_insecure_test_mode);
         auto local_endpoint = p2p_endpoint_for(quic.local_endpoint());
         auto remote_endpoint = p2p_endpoint_for(quic.remote_endpoint());
         co_return connection{
             .peer = remote,
             .session = forge::net::quic::as_transport_session(std::move(quic)),
             .local_endpoint = std::move(local_endpoint),
             .remote_endpoint = std::move(remote_endpoint),
             .admission = std::move(*admission),
         };
      } catch (const forge::exceptions::base& error) {
         rethrow_quic_as_p2p(error);
      }
   }

 private:
   [[nodiscard]] bool listener_is_current(const std::string& key,
                                          const std::shared_ptr<forge::net::quic::listener>& listener) const {
      auto lock = std::scoped_lock{listeners_mutex_};
      const auto found = listeners_.find(key);
      return !listeners_stopped_ && found != listeners_.end() && found->second.active &&
             found->second.value == listener;
   }

   [[nodiscard]] std::vector<std::shared_ptr<forge::net::quic::listener>> stop_listeners() {
      auto listeners = std::vector<std::shared_ptr<forge::net::quic::listener>>{};
      auto lock = std::scoped_lock{listeners_mutex_};
      listeners_stopped_ = true;
      listeners.reserve(listeners_.size());
      for (auto& [_, listener] : listeners_) {
         listener.active = false;
         listeners.push_back(listener.value);
      }
      return listeners;
   }

   [[nodiscard]] std::vector<std::shared_ptr<forge::net::quic::listener>> listener_snapshot() const {
      auto listeners = std::vector<std::shared_ptr<forge::net::quic::listener>>{};
      auto lock = std::scoped_lock{listeners_mutex_};
      listeners.reserve(listeners_.size());
      for (const auto& [_, listener] : listeners_) {
         listeners.push_back(listener.value);
      }
      return listeners;
   }

   void track(const std::shared_ptr<cancellation_latch>& operation) {
      auto cancel_now = false;
      {
         auto lock = std::scoped_lock{active_mutex_};
         cancel_now = stopped_;
         if (!cancel_now) {
            active_.erase(std::remove_if(active_.begin(), active_.end(),
                                         [](const auto& operation) { return operation.expired(); }),
                          active_.end());
            active_.push_back(operation);
         }
      }
      if (cancel_now) {
         operation->request_stop();
      }
   }

   [[nodiscard]] forge::net::quic::security_options
   peer_verifier(std::optional<peer_id> expected = std::nullopt) const {
      if (options_.allow_insecure_test_mode) {
         auto security = forge::net::quic::security_options{.verify_peer = true};
         security.verifier = [](const forge::net::quic::peer_certificate&) { return true; };
         return security;
      }
      auto security = forge::net::quic::security_options{.verify_peer = true};
      security.verifier = [expected = std::move(expected)](const forge::net::quic::peer_certificate& certificate) {
         try {
            const auto remote = make_peer_id_from_certificate_der(certificate.der);
            if (expected) {
               return remote == *expected;
            }
            return valid_peer_id(remote);
         } catch (const forge::exceptions::base&) {
            return false;
         }
      };
      return security;
   }

   [[nodiscard]] forge::net::quic::client_options client_options(std::optional<peer_id> expected,
                                                                 std::chrono::milliseconds timeout) const {
      return forge::net::quic::client_options{
          .alpn = "libp2p",
          .connect_timeout = timeout,
          .handshake_timeout = timeout,
          .limits = quic_limits(options_.transport_limits),
          .security = peer_verifier(std::move(expected)),
          .certificate_pem = options_.certificate_pem,
          .private_key_pem = options_.private_key_pem,
      };
   }

   [[nodiscard]] forge::net::quic::server_options server_options() const {
      return forge::net::quic::server_options{
          .alpn = "libp2p",
          .limits = quic_limits(options_.transport_limits),
          .security = peer_verifier(),
          .certificate_pem = options_.certificate_pem,
          .private_key_pem = options_.private_key_pem,
          .inbound_admission = [resources = resources_]() mutable -> std::shared_ptr<void> {
             auto admission = resources.reserve_session(resource_manager::session_direction::inbound);
             if (!admission) {
                return {};
             }
             return std::make_shared<resource_manager::session_reservation>(std::move(*admission));
          },
      };
   }

   forge::asio::runtime& runtime_;
   const node::options& options_;
   resource_manager resources_;
   mutable std::mutex listeners_mutex_;
   std::map<std::string, listener_entry> listeners_;
   bool listeners_stopped_ = false;
   std::mutex active_mutex_;
   std::vector<std::weak_ptr<cancellation_latch>> active_;
   bool stopped_ = false;
};

} // namespace

void register_quic_profile(registry& value, forge::asio::runtime& runtime, const node::options& options,
                           resource_manager resources) {
   auto owned = std::make_shared<quic_profile>(runtime, options, std::move(resources));
   value.add(profile{
       .supports = [owned](const forge::net::p2p::endpoint& endpoint) { return owned->supports(endpoint); },
       .listening = [owned] { return owned->listening(); },
       .local_endpoints = [owned] { return owned->local_endpoints(); },
       .listen = [owned](forge::net::p2p::endpoint endpoint) { return owned->listen(std::move(endpoint)); },
       .stop = [owned] { owned->stop(); },
       .async_stop = [owned] { return owned->async_stop(); },
       .async_connect =
           [owned](forge::net::p2p::endpoint endpoint, const node::connect_options& options,
                   std::shared_ptr<cancellation_latch> cancellation) {
              return owned->async_connect(std::move(endpoint), options, std::move(cancellation));
           },
       .async_accept = [owned](forge::net::p2p::endpoint endpoint) { return owned->async_accept(std::move(endpoint)); },
   });
}

} // namespace forge::net::p2p::direct
