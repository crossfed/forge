module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

module forge.net.p2p.node;

import forge.asio.runtime;
import forge.crypto.asymmetric;
import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;
import forge.net.p2p.stream;
import forge.net.tcp.connection;
import forge.net.tcp.connector;
import forge.net.tcp.exceptions;
import forge.net.tcp.listener;
import forge.net.stcp.options;
import forge.net.transport.connector;
import forge.net.transport.session;
import forge.net.transport.stream;
import forge.net.yamux.session;

#include "details/direct_transport.hxx"
#include "details/libp2p_tls.hxx"
#include "details/operation_deadline.hxx"
#include "details/stream_upgrade.hxx"

namespace forge::net::p2p::direct {
namespace {

[[nodiscard]] forge::net::p2p::endpoint p2p_endpoint_for(forge::net::transport::endpoint value) {
   return forge::net::p2p::endpoint{.transport = std::move(value)};
}

[[nodiscard]] std::string listener_key(forge::net::p2p::endpoint value) {
   value.peer.reset();
   return value.to_string();
}

[[nodiscard]] exceptions::code map_tcp_error(forge::net::tcp::exceptions::code kind) noexcept {
   using tcp_kind = forge::net::tcp::exceptions::code;
   switch (kind) {
   case tcp_kind::invalid_endpoint:
   case tcp_kind::invalid_options:
      return exceptions::code::invalid_options;
   case tcp_kind::canceled:
      return exceptions::code::canceled;
   case tcp_kind::closed:
      return exceptions::code::closed;
   case tcp_kind::connect_failed:
   case tcp_kind::listen_failed:
   case tcp_kind::accept_failed:
   case tcp_kind::io_error:
      return exceptions::code::internal;
   }
   return exceptions::code::internal;
}

[[noreturn]] void rethrow_tcp_as_p2p(const forge::exceptions::base& error) {
   const auto code = forge::net::tcp::exceptions::code_of(error);
   if (code) {
      FORGE_THROW_CODE(map_tcp_error(*code), error.what());
   }
   throw;
}

[[nodiscard]] std::optional<peer_id> expected_peer_for(const forge::net::p2p::endpoint& endpoint,
                                                       const node::connect_options& options) {
   if (options.expected_peer) {
      return options.expected_peer;
   }
   return endpoint.peer;
}

struct cancel_current_scope {
   std::shared_ptr<cancellation_latch> value;

   ~cancel_current_scope() {
      if (value) {
         value->clear();
      }
   }
};

class tcp_profile final {
   struct listener_entry {
      std::unique_ptr<forge::net::tcp::listener> value;
      bool active = true;
   };

 public:
   tcp_profile(forge::asio::runtime& runtime_value, const node::options& options_value,
               const libp2p_identity_material& identity_value)
       : runtime_(runtime_value), options_(options_value), identity_(identity_value) {}

   [[nodiscard]] bool supports(const forge::net::p2p::endpoint& endpoint) const noexcept {
      return endpoint.is_direct_tcp();
   }

   [[nodiscard]] bool listening() const noexcept {
      return std::ranges::any_of(listeners_, [](const auto& item) { return item.second.active; });
   }

   [[nodiscard]] std::vector<forge::net::p2p::endpoint> local_endpoints() const {
      auto out = std::vector<forge::net::p2p::endpoint>{};
      out.reserve(listeners_.size());
      for (const auto& [_, listener] : listeners_) {
         if (listener.active && listener.value->valid()) {
            out.push_back(p2p_endpoint_for(listener.value->local_endpoint()));
         }
      }
      return out;
   }

   forge::net::p2p::endpoint listen(forge::net::p2p::endpoint endpoint) {
      if (!endpoint.is_direct_tcp()) {
         FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct TCP endpoint");
      }
      if (endpoint.transport.port != 0) {
         auto found = listeners_.find(listener_key(endpoint));
         if (found != listeners_.end() && found->second.active) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P TCP direct listener endpoint is already active");
         }
      }
      try {
         auto listener =
             std::make_unique<forge::net::tcp::listener>(runtime_.context().get_executor(), endpoint.transport);
         auto local = p2p_endpoint_for(listener->local_endpoint());
         const auto key = listener_key(local);
         auto found = listeners_.find(key);
         if (found != listeners_.end() && found->second.active) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P TCP direct listener endpoint is already active");
         }
         listeners_[key] = listener_entry{.value = std::move(listener), .active = true};
         return local;
      } catch (const forge::exceptions::base& error) {
         rethrow_tcp_as_p2p(error);
      }
   }

   void stop() {
      for (auto& [_, listener] : listeners_) {
         listener.active = false;
         listener.value->close();
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
         operation->cancel();
      }
   }

   boost::asio::awaitable<void> async_stop() {
      stop();
      for (auto& [_, listener] : listeners_) {
         co_await listener.value->async_close();
      }
   }

   boost::asio::awaitable<connection> async_connect(forge::net::p2p::endpoint endpoint,
                                                    const node::connect_options& options) {
      if (!endpoint.is_direct_tcp()) {
         FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct TCP endpoint");
      }
      auto expected_peer = expected_peer_for(endpoint, options);
      auto remote_transport = endpoint.transport;
      auto connector = forge::net::tcp::connector{runtime_.context().get_executor()};
      auto cancel_current = std::make_shared<cancellation_latch>();
      track(cancel_current);
      cancel_current->set([&connector] { connector.cancel(); });
      auto deadline = operation_deadline{runtime_.context(), options.timeout};
      auto cancel_scope = cancel_current_scope{cancel_current};
      deadline.arm([cancel_current] { cancel_current->cancel(); });
      try {
         auto tcp = co_await connector.async_connect_connection(std::move(remote_transport));
         cancel_current->set([&tcp] { tcp.cancel(); });
         const auto local_endpoint = p2p_endpoint_for(tcp.local_endpoint());
         const auto remote_endpoint = p2p_endpoint_for(tcp.remote_endpoint());
         auto upgraded = co_await upgrade_outbound_tcp(std::move(tcp), options_, identity_, std::move(expected_peer),
                                                       tcp_upgrade_deadline{.context = &runtime_.context(),
                                                                            .timeout = options.timeout,
                                                                            .cancel_current = cancel_current});
         if (!deadline.finish()) {
            throw_operation_timeout("P2P TCP direct connect");
         }
         co_return connection{
             .peer = std::move(upgraded.peer),
             .session = std::move(*upgraded.session).as_transport(),
             .local_endpoint = std::move(local_endpoint),
             .remote_endpoint = std::move(remote_endpoint),
         };
      } catch (const forge::exceptions::base& error) {
         if (deadline.timed_out()) {
            throw_operation_timeout("P2P TCP direct connect");
         }
         rethrow_tcp_as_p2p(error);
      }
   }

   boost::asio::awaitable<connection> async_accept(forge::net::p2p::endpoint endpoint) {
      auto found = listeners_.find(listener_key(std::move(endpoint)));
      if (found == listeners_.end() || !found->second.active || !found->second.value->valid()) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "P2P TCP direct listener is not active");
      }
      try {
         auto tcp = co_await found->second.value->async_accept_connection();
         const auto local_endpoint = p2p_endpoint_for(tcp.local_endpoint());
         const auto remote_endpoint = p2p_endpoint_for(tcp.remote_endpoint());
         auto cancel_current = std::make_shared<cancellation_latch>();
         track(cancel_current);
         cancel_current->set([&tcp] { tcp.cancel(); });
         auto deadline = operation_deadline{runtime_.context(), node::connect_options{}.timeout};
         auto cancel_scope = cancel_current_scope{cancel_current};
         deadline.arm([cancel_current] { cancel_current->cancel(); });
         auto upgraded = upgraded_session{};
         try {
            upgraded = co_await upgrade_inbound_tcp(std::move(tcp), options_, identity_, std::nullopt,
                                                    tcp_upgrade_deadline{.context = &runtime_.context(),
                                                                         .timeout = node::connect_options{}.timeout,
                                                                         .cancel_current = cancel_current});
            if (!deadline.finish()) {
               throw_operation_timeout("P2P TCP direct accept");
            }
         } catch (const forge::exceptions::base&) {
            if (deadline.timed_out()) {
               throw_operation_timeout("P2P TCP direct accept");
            }
            throw;
         }
         co_return connection{
             .peer = std::move(upgraded.peer),
             .session = std::move(*upgraded.session).as_transport(),
             .local_endpoint = std::move(local_endpoint),
             .remote_endpoint = std::move(remote_endpoint),
         };
      } catch (const forge::exceptions::base& error) {
         rethrow_tcp_as_p2p(error);
      }
   }

 private:
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
         operation->cancel();
      }
   }

   forge::asio::runtime& runtime_;
   const node::options& options_;
   const libp2p_identity_material& identity_;
   std::map<std::string, listener_entry> listeners_;
   std::mutex active_mutex_;
   std::vector<std::weak_ptr<cancellation_latch>> active_;
   bool stopped_ = false;
};

} // namespace

void register_tcp_profile(registry& value, forge::asio::runtime& runtime, const node::options& options,
                          const libp2p_identity_material& identity) {
   auto owned = std::make_shared<tcp_profile>(runtime, options, identity);
   value.add(profile{
       .supports = [owned](const forge::net::p2p::endpoint& endpoint) { return owned->supports(endpoint); },
       .listening = [owned] { return owned->listening(); },
       .local_endpoints = [owned] { return owned->local_endpoints(); },
       .listen = [owned](forge::net::p2p::endpoint endpoint) { return owned->listen(std::move(endpoint)); },
       .stop = [owned] { owned->stop(); },
       .async_stop = [owned] { return owned->async_stop(); },
       .async_connect =
           [owned](forge::net::p2p::endpoint endpoint, const node::connect_options& options) {
              return owned->async_connect(std::move(endpoint), options);
           },
       .async_accept = [owned](forge::net::p2p::endpoint endpoint) { return owned->async_accept(std::move(endpoint)); },
   });
}

} // namespace forge::net::p2p::direct
