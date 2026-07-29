module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.plugins.p2p.node.plugin;

import forge.api.transport.options;
import forge.asio.runtime;
import forge.asio.task;
import forge.exceptions;
import forge.net.p2p.diagnostics;
import forge.net.p2p.endpoint;
import forge.net.p2p.identity;
import forge.net.p2p.node;
import forge.net.p2p.protocol;
import forge.net.p2p.pubsub;
import forge.net.p2p.scoring;
import forge.plugins.p2p.node.exceptions;
import forge.plugins.p2p.node.types;

#include "details/plugin_impl.hxx"
#include "details/config.hxx"

namespace forge::plugins::p2p::node {
namespace {

inline constexpr auto bootstrap_scan_interval = std::chrono::seconds{1};
inline constexpr auto bootstrap_connect_timeout = std::chrono::seconds{2};
inline constexpr auto bootstrap_retry_max = std::chrono::seconds{30};
inline constexpr auto bootstrap_protection_tag = "bootstrap";

[[nodiscard]] bool contains_protocol(
    const std::vector<std::pair<forge::net::p2p::protocol_id, forge::net::p2p::node::protocol_handler>>& routes,
    const forge::net::p2p::protocol_id& protocol) {
   return std::any_of(routes.begin(), routes.end(), [&](const auto& route) { return route.first == protocol; });
}

[[nodiscard]] std::chrono::milliseconds bootstrap_retry_delay(std::size_t failures, std::chrono::milliseconds step,
                                                              std::chrono::milliseconds maximum) {
   if (failures == 0) {
      return bootstrap_scan_interval;
   }
   auto delay = step;
   for (auto attempt = std::size_t{1}; attempt < failures && delay < maximum; ++attempt) {
      delay = delay > maximum - delay ? maximum : delay + delay;
   }
   return std::min(delay, maximum);
}

[[nodiscard]] bool has_active_session(const forge::net::p2p::node& node, const forge::net::p2p::peer_id& peer,
                                      std::size_t max_sessions) {
   const auto snapshot = node.diagnostics(forge::net::p2p::diagnostics::options{
       .max_sessions = max_sessions,
   });
   return std::any_of(snapshot.sessions.begin(), snapshot.sessions.end(),
                      [&](const auto& session) { return !session.closed && session.remote_peer == peer; });
}

} // namespace

forge::net::p2p::node& plugin::impl::ensure_node() {
   if (!runtime) {
      FORGE_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
   }
   if (!node) {
      if (pubsub_requested) {
         options.capabilities.add(forge::net::p2p::capabilities::pubsub);
         options.limits.pubsub = pubsub_options;
      }
      node = std::make_unique<forge::net::p2p::node>(*runtime, options);
   }
   return *node;
}

forge::net::p2p::node& plugin::impl::require_node() {
   if (!node) {
      FORGE_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
   }
   return *node;
}

const forge::net::p2p::node& plugin::impl::require_node() const {
   if (!node) {
      FORGE_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
   }
   return *node;
}

boost::asio::awaitable<void> plugin::impl::refresh_bootstrap() {
   auto& active_node = require_node();
   for (auto& configured : bootstrap) {
      if (stopping.load(std::memory_order_acquire)) {
         co_return;
      }
      const auto now = std::chrono::steady_clock::now();
      if (configured.next_attempt > now) {
         continue;
      }
      if (configured.peer && has_active_session(active_node, *configured.peer, options.limits.max_sessions)) {
         configured.failures = 0;
         configured.next_attempt = now + bootstrap_scan_interval;
         active_node.protect_peer(*configured.peer, bootstrap_protection_tag);
         continue;
      }

      try {
         const auto session = co_await active_node.async_connect(
             configured.endpoint, forge::net::p2p::node::connect_options{
                                      .expected_peer = configured.peer,
                                      .allow_relay = false,
                                      .timeout = bootstrap_connect_timeout,
                                      .direct_attempt_timeout = bootstrap_connect_timeout,
                                      .allow_hole_punch = false,
                                  });
         configured.peer = session.remote_peer;
         configured.failures = 0;
         configured.next_attempt = std::chrono::steady_clock::now() + bootstrap_scan_interval;
         active_node.protect_peer(session.remote_peer, bootstrap_protection_tag);
      } catch (...) {
         if (configured.failures < std::numeric_limits<std::size_t>::max()) {
            ++configured.failures;
         }
         configured.next_attempt =
             std::chrono::steady_clock::now() +
             bootstrap_retry_delay(
                 configured.failures, options.limits.dial_backoff_step,
                 std::min(options.limits.dial_backoff_max,
                          std::chrono::duration_cast<std::chrono::milliseconds>(bootstrap_retry_max)));
         forge::exceptions::capture_and_log("P2P bootstrap connect failed");
      }
   }
}

void plugin::impl::start_bootstrap_maintenance() {
   if (bootstrap.empty()) {
      return;
   }
   if (!scheduler) {
      FORGE_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P node plugin scheduler is not initialized");
   }
   auto self = shared_from_this();
   auto maintenance = scheduler->submit(forge::asio::task::awaitable{
       .name = "p2p-bootstrap-maintenance",
       .work = [self = std::move(self)](forge::asio::task::context& context) -> boost::asio::awaitable<void> {
          auto executor = co_await boost::asio::this_coro::executor;
          auto lane = boost::asio::make_strand(executor);
          co_await boost::asio::co_spawn(
              lane,
              [self, &context]() -> boost::asio::awaitable<void> {
                 auto executor = co_await boost::asio::this_coro::executor;
                 while (!context.cancel_requested() && !self->stopping.load(std::memory_order_acquire)) {
                    co_await self->refresh_bootstrap();
                    if (context.cancel_requested() || self->stopping.load(std::memory_order_acquire)) {
                       co_return;
                    }

                    auto timer = std::make_shared<boost::asio::steady_timer>(executor, bootstrap_scan_interval);
                    auto stop = std::stop_callback{context.stop_token(), [executor, timer] {
                                                      boost::asio::post(executor, [timer] { timer->cancel(); });
                                                   }};
                    auto error = boost::system::error_code{};
                    co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
                    if (error && error != boost::asio::error::operation_aborted) {
                       throw boost::system::system_error{error};
                    }
                 }
              },
              boost::asio::use_awaitable);
       },
   });

   auto lock = std::scoped_lock{bootstrap_maintenance_mutex};
   bootstrap_maintenance = std::move(maintenance);
   if (stopping.load(std::memory_order_acquire)) {
      bootstrap_maintenance.cancel();
   }
}

void plugin::impl::request_bootstrap_stop() noexcept {
   stopping.store(true, std::memory_order_release);
   auto lock = std::scoped_lock{bootstrap_maintenance_mutex};
   if (bootstrap_maintenance.valid()) {
      bootstrap_maintenance.cancel();
   }
}

boost::asio::awaitable<void> plugin::impl::stop_bootstrap_maintenance() {
   auto maintenance = forge::asio::task::handle{};
   {
      auto lock = std::scoped_lock{bootstrap_maintenance_mutex};
      if (!bootstrap_maintenance.valid()) {
         co_return;
      }
      maintenance = std::move(bootstrap_maintenance);
   }
   try {
      co_await maintenance.wait();
   } catch (const forge::asio::exceptions::canceled&) {
   }
}

void plugin::impl::add_route(forge::net::p2p::protocol_id protocol, forge::net::p2p::node::protocol_handler handler) {
   if (started) {
      FORGE_THROW_EXCEPTION(exceptions::route_conflict, "P2P routes must be published before startup",
                            forge::exceptions::ctx("protocol", protocol.value));
   }
   if (protocol.value.empty() || !handler) {
      FORGE_THROW_EXCEPTION(exceptions::route_conflict, "P2P route is invalid");
   }
   if (contains_protocol(routes, protocol)) {
      FORGE_THROW_EXCEPTION(exceptions::route_conflict, "duplicate P2P route",
                            forge::exceptions::ctx("protocol", protocol.value));
   }
   routes.emplace_back(std::move(protocol), std::move(handler));
}

forge::net::p2p::node::open_options plugin::impl::open_options_for(remote_options value) const {
   if (value.open_deadline.count() <= 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "P2P remote open deadline must be positive");
   }
   return forge::net::p2p::node::open_options{
       .allow_relay = policy.relay_client_enabled && policy.path.allow_relay,
       .timeout = value.open_deadline,
       .direct_attempt_timeout = std::min(std::chrono::milliseconds{2'000}, value.open_deadline),
       .relay_attempt_timeout = std::min(std::chrono::milliseconds{5'000}, value.open_deadline),
       .max_direct_endpoints = policy.path.max_direct_endpoints,
       .max_relay_candidates = policy.path.max_relay_candidates,
       .allow_hole_punch = policy.relay_client_enabled && policy.path.allow_hole_punch,
   };
}

forge::api::transport::options plugin::impl::api_options_for(const remote_options& value) const {
   auto out = api_options;
   if (value.codec.has_value()) {
      if (value.codec->value.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "P2P remote API codec override is invalid");
      }
      out.codec = *value.codec;
   }
   if (value.max_inflight.has_value()) {
      if (*value.max_inflight == 0) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "P2P remote API max inflight override is invalid");
      }
      out.max_inflight = *value.max_inflight;
   }
   if (value.deadline.has_value()) {
      out.deadline = *value.deadline;
   }
   if (value.max_frame_size.has_value()) {
      if (*value.max_frame_size == 0) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "P2P remote API max frame size override is invalid");
      }
      out.max_frame_size = *value.max_frame_size;
   }
   return out;
}

} // namespace forge::plugins::p2p::node
