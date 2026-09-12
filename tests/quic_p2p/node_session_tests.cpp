module;

#include <boost/test/unit_test.hpp>
#include <boost/asio/use_future.hpp>
#include <future>
#include <thread>
#include "libp2p_identity_fixture.hxx"

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
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
#include <boost/compat/move_only_function.hpp>

module forge.net.p2p.node;

import forge.exceptions;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.asio.gate;
import forge.asio.notification;
import forge.crypto.asymmetric;
import forge.net.p2p.dht;
import forge.net.p2p.discovery;
import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;
import forge.net.p2p.hole_punch;
import forge.net.p2p.identify;
import forge.net.p2p.identity;
import forge.net.p2p.lifecycle;
import forge.net.p2p.message;
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
import forge.multiformats.multiaddr;
import forge.net.transport.session;
import forge.net.transport.stream;
import forge.net.yamux.session;

#include "../../libraries/net/p2p/details/direct_transport.hxx"
#include "../../libraries/net/p2p/details/cancellation_latch.hxx"
#include "../../libraries/net/p2p/details/node_impl.hxx"
#include "../../libraries/net/p2p/details/owner_cancellation.hxx"
#include "../../libraries/net/p2p/details/path_selector.hxx"
#include "../../libraries/net/p2p/details/peer_exchange_codec.hxx"
#include "../../libraries/net/p2p/details/peer_failure.hxx"
#include "../../libraries/net/p2p/details/resource_stream.hxx"
#include "../../libraries/net/p2p/details/session_lifecycle.hxx"
#include "../../libraries/net/p2p/details/session_retirement.hxx"


namespace forge::net::p2p {
namespace {

struct close_barrier {
   forge::asio::notification changed;
   std::atomic_bool entered{false};
   std::atomic_bool released{false};
   std::atomic_size_t opens{0};
   std::atomic_size_t accepts{0};
};

class admission_transport final : public forge::net::transport::detail::session_concept {
 public:
   admission_transport(std::shared_ptr<close_barrier> state, std::shared_ptr<void> native)
       : state_(std::move(state)), native_(std::move(native)) {}

   bool valid() const noexcept override { return open_.load(); }

   boost::asio::awaitable<forge::net::transport::stream> async_open_stream() override {
      ++state_->opens;
      FORGE_THROW_EXCEPTION(exceptions::connection_rejected, "fixture rejects native stream open");
      co_return forge::net::transport::stream{};
   }

   boost::asio::awaitable<forge::net::transport::stream> async_accept_stream() override {
      ++state_->accepts;
      co_return forge::net::transport::stream{};
   }

   boost::asio::awaitable<void> async_close() override {
      state_->entered = true;
      for (;;) {
         const auto epoch = state_->changed.epoch();
         if (state_->released.load()) {
            break;
         }
         co_await state_->changed.async_wait(epoch);
      }
      open_ = false;
   }

   void cancel() override { open_ = false; }

 private:
   std::shared_ptr<close_barrier> state_;
   std::shared_ptr<void> native_;
   std::atomic_bool open_{true};
};

template <typename Predicate>
bool drive_until(boost::asio::io_context& context, Predicate predicate,
                 std::chrono::milliseconds timeout = std::chrono::seconds{2}) {
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   while (!predicate() && std::chrono::steady_clock::now() < deadline) {
      context.restart();
      context.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
   }
   return predicate();
}

node::options fixture_options(std::string_view name) {
   auto identity = forge::tests::p2p::make_identity_fixture(name);
   auto options = node::options{};
   options.certificate_pem = std::move(identity.certificate_pem);
   options.private_key_pem = std::move(identity.private_key_pem);
   options.peer_state.persistence = peer_store::make_memory_persistence();
   return options;
}

} // namespace

// The friendship permits this test TU to reach existing admission and session
// state. No production option, runtime callback or alternate admission is added.
struct node_session_fixture {
   static void removed_cached_session_has_one_fresh_dial() {
      auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
      auto server_options = fixture_options("cached-removal-server");
      server_options.limits.resources.system.max_streams = 1;
      auto server = node{runtime, std::move(server_options)};
      auto client = node{runtime, fixture_options("cached-removal-client")};
      auto self = client.impl_;
      auto server_self = server.impl_;

      // Occupy the real P2P stream budget before either session exists. TCP
      // authentication/Yamux upgrade still work; inbound protocol streams reset.
      auto held = server_self->resources.reserve_stream(
          client.local_peer(), resource_manager::session_direction::inbound);
      auto cleanup = std::unique_ptr<void, std::function<void(void*)>>{self.get(), [&](void*) noexcept {
         if (held) {
            held->release();
         }
         client.request_stop();
         server.request_stop();
         for (auto* owner : {&client, &server}) {
            try {
               forge::asio::blocking::run(runtime, owner->async_stop());
            } catch (...) {
               BOOST_ERROR("cached-session regression failed to join node shutdown");
            }
         }
      }};
      BOOST_REQUIRE(held);
      server.register_protocol_handler(builtins::echo,
          [](node::incoming_protocol_stream incoming) -> boost::asio::awaitable<void> {
             co_await incoming.stream.async_close();
          });
      forge::asio::blocking::run(runtime, server.async_listen(parse_endpoint("/ip4/127.0.0.1/tcp/0")));
      const auto listening = server.local_endpoint();
      BOOST_REQUIRE(listening);
      const auto remote = server.local_peer();
      const auto connected = forge::asio::blocking::run(
          runtime, client.async_connect(*listening,
              node::connect_options{.expected_peer = remote, .allow_relay = false,
                                    .timeout = std::chrono::seconds{10},
                                    .direct_attempt_timeout = std::chrono::seconds{5},
                                    .max_direct_endpoints = 1, .allow_hole_punch = false}));
      BOOST_CHECK(connected.remote_peer == remote);
      auto cached = self->session_for_path(remote, path::kind::direct);
      auto inbound = server_self->session_for_path(client.local_peer(), path::kind::direct);
      BOOST_REQUIRE(cached);
      BOOST_REQUIRE(inbound);
      BOOST_REQUIRE(cached->connection.valid());
      BOOST_REQUIRE_EQUAL(client.metrics().handshakes_completed, 1U);
      BOOST_REQUIRE_EQUAL(server.metrics().handshakes_completed, 1U);

      // This real wrapper selects synchronously and owns peer/protocol values.
      // Do not run its awaitable until terminal retirement has removed the
      // selected session: no sleeps, registry hooks or copied retry loop.
      auto pending = self->open_protocol_direct_with_context(
          peer_id{remote}, protocol_id{builtins::echo}, std::chrono::seconds{15}, 1,
          std::chrono::seconds{5}, {});
      self->forget_session(cached);
      forge::asio::blocking::run(runtime, self->async_retire_session(cached, true));
      server_self->forget_session(inbound);
      forge::asio::blocking::run(runtime, server_self->async_retire_session(inbound, true));
      BOOST_REQUIRE(!self->session_for_path(remote, path::kind::direct));
      BOOST_REQUIRE(!cached->connection.valid());
      const auto before = client.metrics();
      const auto server_before = server.metrics();
      BOOST_REQUIRE_EQUAL(before.active_sessions, 0U);
      BOOST_REQUIRE_EQUAL(server_before.active_sessions, 0U);

      // A dial/handshake failure or unsupported protocol is not this regression.
      // The replacement must handshake, then fail on a real transport stream.
      BOOST_CHECK_EXCEPTION(
          static_cast<void>(forge::asio::blocking::run(runtime, std::move(pending))),
          forge::exceptions::base,
          [](const auto& error) { return exceptions::code_of(error) == exceptions::code::closed; });
      const auto after = client.metrics();
      const auto server_after = server.metrics();
      const auto fresh_dials = after.sessions_opened - before.sessions_opened;
      BOOST_TEST(fresh_dials >= 1U);
      BOOST_TEST(fresh_dials <= 1U);
      BOOST_TEST(after.handshakes_completed - before.handshakes_completed == 1U);
      BOOST_TEST(server_after.sessions_opened - server_before.sessions_opened == 1U);
      BOOST_TEST(server_after.handshakes_completed - server_before.handshakes_completed == 1U);
      BOOST_TEST(after.handshakes_failed == before.handshakes_failed);
      BOOST_TEST(server_after.handshakes_failed == server_before.handshakes_failed);
      // One fresh dial plus the stale and fresh protocol-open attempts.
      BOOST_TEST(after.path_direct_attempts - before.path_direct_attempts == 3U);
      BOOST_TEST(after.protocol_streams_opened == before.protocol_streams_opened);
      BOOST_TEST(server_after.protocol_streams_accepted == server_before.protocol_streams_accepted);
      BOOST_TEST(server_after.backpressure_rejections > server_before.backpressure_rejections);
      BOOST_TEST(server_self->resources.current().system.inbound_streams == 1U);
   }

   static void blocked_admission(bool timeout) {
      auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
      auto owner = node{runtime, fixture_options("admission-owner")};
      auto self = owner.impl_;
      auto context = boost::asio::io_context{};
      auto held = forge::asio::blocking::run(runtime, self->session_admission_gate.acquire());
      auto cancellation = std::make_shared<cancellation_latch>();
      auto barrier = std::make_shared<close_barrier>();

      auto session = self->resources.reserve_session(resource_manager::session_direction::outbound);
      BOOST_REQUIRE(session);
      auto fd = session->reserve_file_descriptors(1);
      BOOST_REQUIRE(fd);
      auto attempt = detail::direct_attempt{};
      attempt.resources = std::make_shared<detail::direct_attempt_resources>();
      attempt.resources->teardown_ticket = self->teardown.track();
      attempt.resources->session = std::move(*session);
      attempt.resources->file_descriptor = std::move(*fd);
      auto resources_weak = std::weak_ptr<detail::direct_attempt_resources>{attempt.resources};
      auto transport = std::make_shared<admission_transport>(barrier, attempt.resources);
      auto transport_weak = std::weak_ptr<admission_transport>{transport};
      attempt.connection.peer = make_peer_id(public_key{public_key::type::ed25519, std::vector<std::uint8_t>(32, 43)});
      attempt.connection.session = forge::net::transport::detail::session_access::make(transport);
      transport.reset();
      attempt.target = parse_endpoint("/ip4/127.0.0.1/tcp/4001/p2p/" + attempt.connection.peer.to_string());
      attempt.started_at = std::chrono::steady_clock::now();
      auto roots = std::vector<forge::multiformats::multiaddr>{attempt.target.to_multiaddr()};
      const auto remote = attempt.connection.peer;
      const auto deadline = std::chrono::steady_clock::now() +
          (timeout ? std::chrono::milliseconds{400} : std::chrono::seconds{5});

      auto cleanup = std::unique_ptr<void, std::function<void(void*)>>{self.get(), [&](void*) {
         barrier->released = true;
         barrier->changed.notify();
         cancellation->request_stop();
         held.release();
         owner.request_stop();
         context.restart();
         context.run_for(std::chrono::seconds{2});
         forge::asio::blocking::run(runtime, owner.async_stop());
      }};
      auto pending = boost::asio::co_spawn(
          context, self->commit_direct_attempt(std::move(attempt), std::move(roots), deadline, cancellation),
          boost::asio::use_future);
      context.poll();
      // Drain all ready handlers, not one post: the only unfinished child work
      // is now the acquisition of the occupied gate.
      BOOST_REQUIRE(pending.wait_for(std::chrono::milliseconds{0}) == std::future_status::timeout);
      BOOST_REQUIRE(std::chrono::steady_clock::now() < deadline);
      BOOST_REQUIRE(!barrier->entered.load());
      BOOST_REQUIRE(!cancellation->stop_requested());
      if (!timeout) {
         cancellation->request_stop();
      }
      BOOST_REQUIRE(drive_until(context, [&] { return barrier->entered.load(); }));
      BOOST_CHECK(pending.wait_for(std::chrono::milliseconds{0}) == std::future_status::timeout);
      BOOST_TEST(self->resources.current().system.file_descriptors == 1U);
      BOOST_TEST(self->resources.current().system.outbound_connections == 1U);
      BOOST_TEST(!transport_weak.expired());
      BOOST_TEST(!resources_weak.expired());
      BOOST_TEST(barrier->opens.load() == 0U);
      BOOST_TEST(barrier->accepts.load() == 0U);
      BOOST_TEST(self->sessions.empty());
      BOOST_TEST(self->retiring_sessions.empty());

      barrier->released = true;
      barrier->changed.notify();
      BOOST_REQUIRE(drive_until(context, [&] {
         return pending.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready;
      }));
      // The gate is deliberately still held: cancellation/timeout, not release,
      // must have completed the operation and its terminal cleanup.
      BOOST_CHECK_EXCEPTION(pending.get(), forge::exceptions::base, [timeout](const auto& error) {
         return exceptions::code_of(error) ==
             (timeout ? exceptions::code::timeout : exceptions::code::canceled);
      });
      BOOST_TEST(transport_weak.expired());
      BOOST_TEST(resources_weak.expired());
      BOOST_TEST(self->resources.current().system.file_descriptors == 0U);
      BOOST_TEST(self->resources.current().system.outbound_connections == 0U);
      BOOST_TEST(!self->store.find(remote).has_value());
      BOOST_TEST(self->sessions.empty());
      BOOST_TEST(self->retiring_sessions.empty());
      BOOST_TEST(barrier->opens.load() == 0U);
      BOOST_TEST(barrier->accepts.load() == 0U);
      held.release();
      auto reacquired = forge::asio::blocking::run(runtime, self->session_admission_gate.acquire());
      reacquired.release();
   }
};

BOOST_AUTO_TEST_CASE(p2p_dial_cancellation_interrupts_occupied_admission_and_awaits_native_close) {
   node_session_fixture::blocked_admission(false);
}

BOOST_AUTO_TEST_CASE(p2p_dial_deadline_interrupts_occupied_admission_and_awaits_native_close) {
   node_session_fixture::blocked_admission(true);
}

BOOST_AUTO_TEST_CASE(p2p_cached_session_removed_before_open_allows_only_one_fresh_handshaken_dial) {
   node_session_fixture::removed_cached_session_has_one_fresh_dial();
}

} // namespace forge::net::p2p
