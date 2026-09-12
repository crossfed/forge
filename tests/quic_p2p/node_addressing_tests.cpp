#include <boost/test/unit_test.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/system/system_error.hpp>

#include "../fixtures/local_dns_server.hxx"
#include "libp2p_identity_fixture.hxx"

import forge.asio.blocking;
import forge.asio.runtime;
import forge.multiformats.multiaddr;
import forge.net.dns.types;
import forge.net.p2p.connection_gater;
import forge.net.p2p.diagnostics;
import forge.net.p2p.dialing;
import forge.net.p2p.endpoint;
import forge.net.p2p.identity;
import forge.net.p2p.node;
import forge.net.p2p.peer_store;
import forge.net.p2p.private_network;
import forge.net.p2p.protocol;
import forge.net.p2p.resource_manager;
import forge.net.p2p.stream;
import forge.net.pnet.protector;

namespace {

namespace asio = boost::asio;
namespace p2p = forge::net::p2p;
namespace dns_fixture = forge::tests::dns;
using tcp = asio::ip::tcp;
using namespace std::chrono_literals;

class ipv4_dial_gate final : public p2p::connection_gater {
 public:
   ipv4_dial_gate() : entered(entered_.get_future()), released_(release_.get_future().share()) {}

   bool intercept_peer_dial(const p2p::peer_id&) noexcept override {
      ++peer_calls;
      return true;
   }

   bool intercept_address_dial(const p2p::peer_id&, const p2p::endpoint& address) noexcept override {
      if (address.transport.host_type == p2p::endpoint::host_kind::ip6 && address.transport.host == "::1") {
         ++ipv6_calls;
         return true;
      }
      if (address.transport.host_type == p2p::endpoint::host_kind::ip4 && address.transport.host == "127.0.0.1") {
         if (ipv4_calls.fetch_add(1) == 0) {
            entered_.set_value();
         }
         return released_.wait_for(8s) == std::future_status::ready;
      }
      unexpected_address = true;
      return false;
   }

   void release() noexcept {
      if (!released_once_.exchange(true)) {
         release_.set_value();
      }
   }

 private:
   std::promise<void> entered_;
   std::promise<void> release_;

 public:
   std::future<void> entered;
   std::atomic_size_t peer_calls{0};
   std::atomic_size_t ipv4_calls{0};
   std::atomic_size_t ipv6_calls{0};
   std::atomic_bool unexpected_address{false};

 private:
   std::shared_future<void> released_;
   std::atomic_bool released_once_{false};
};

class stalled_ipv6_peer final {
 public:
   stalled_ipv6_peer(forge::asio::runtime& runtime, std::uint16_t port)
       : state_(std::make_shared<state>(runtime, port)), accepted(state_->accepted.get_future()),
         finished(asio::co_spawn(state_->executor, run(state_), asio::use_future)) {}

   ~stalled_ipv6_peer() {
      if (!finished.valid()) {
         return;
      }
      asio::post(state_->executor, [state = state_] {
         auto ignored = boost::system::error_code{};
         state->acceptor.close(ignored);
         state->socket.close(ignored);
      });
      try {
         static_cast<void>(finished.get());
      } catch (...) {
         // Failed assertions still cancel and join the owned socket worker.
      }
   }

 private:
   struct state {
      state(forge::asio::runtime& runtime, std::uint16_t port)
          : executor(asio::make_strand(runtime.context())), acceptor(executor), socket(executor) {
         acceptor.open(tcp::v6());
         acceptor.set_option(asio::ip::v6_only{true});
         acceptor.bind(tcp::endpoint{asio::ip::address_v6::loopback(), port});
         acceptor.listen();
      }

      asio::strand<asio::io_context::executor_type> executor;
      tcp::acceptor acceptor;
      tcp::socket socket;
      std::promise<void> accepted;
   };

   static asio::awaitable<std::size_t> run(std::shared_ptr<state> state) {
      co_await state->acceptor.async_accept(state->socket, asio::use_awaitable);
      state->acceptor.close();
      state->accepted.set_value();
      auto received = std::size_t{};
      auto buffer = std::array<std::uint8_t, 4096>{};
      // Consume handshake bytes without replying. Only client terminal close
      // ends the successful path; no timer closes the accepted socket for it.
      for (;;) {
         auto error = boost::system::error_code{};
         received += co_await state->socket.async_read_some(asio::buffer(buffer),
                                                           asio::redirect_error(asio::use_awaitable, error));
         if (error == asio::error::eof || error == asio::error::connection_reset) {
            state->socket.close();
            co_return received;
         }
         if (error) {
            throw boost::system::system_error{error};
         }
      }
   }

   std::shared_ptr<state> state_;

 public:
   std::future<void> accepted;
   std::future<std::size_t> finished;
};

p2p::node::options addressing_options(std::string_view name, bool private_profile) {
   auto identity = forge::tests::p2p::make_identity_fixture(name);
   auto options = p2p::node::options{};
   options.certificate_pem = std::move(identity.certificate_pem);
   options.private_key_pem = std::move(identity.private_key_pem);
   options.allow_insecure_test_mode = false;
   options.peer_state.persistence = p2p::peer_store::make_memory_persistence();
   if (private_profile) {
      auto key = std::array<std::uint8_t, forge::net::pnet::pre_shared_key_size>{};
      for (auto index = std::size_t{}; index < key.size(); ++index) {
         key[index] = static_cast<std::uint8_t>(index);
      }
      options.private_network = p2p::private_network::options{
          .protector = std::make_shared<const forge::net::pnet::protector>(forge::net::pnet::pre_shared_key{key})};
      options.capabilities = p2p::capability_set{.bits = p2p::capabilities::peer_exchange};
      options.relay_policy.service_enabled = false;
      options.relay_policy.client_enabled = false;
      options.relay_policy.auto_discovery_enabled = false;
      options.path_policy.allow_relay = false;
      options.path_policy.allow_hole_punch = false;
   }
   return options;
}

asio::awaitable<void> echo_once(p2p::node::incoming_protocol_stream incoming) {
   const auto frame = co_await incoming.stream.async_read_frame();
   co_await incoming.stream.async_write_frame(frame);
}

} // namespace

BOOST_AUTO_TEST_SUITE(node_addressing_tests)

BOOST_AUTO_TEST_CASE(p2p_dual_stack_dns_tcp_race_preserves_logical_ownership_and_drains_loser) {
   for (const auto private_profile : {false, true}) {
      BOOST_TEST_CONTEXT("private profile=" << private_profile) {
         auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
         auto a_queries = std::make_shared<std::atomic_size_t>(0);
         auto aaaa_queries = std::make_shared<std::atomic_size_t>(0);
         auto dns = dns_fixture::local_dns_server{
             [a_queries, aaaa_queries](const std::uint8_t* data, std::size_t size) -> std::optional<dns_fixture::bytes> {
                const auto question = dns_fixture::parse_question(data, size);
                if (!question) {
                   return std::nullopt;
                }
                if (question->name != "dual-stack.test") {
                   return dns_fixture::make_failure_response(data, *question, 3);
                }
                auto answers = std::vector<dns_fixture::response_answer>{};
                if (question->type == 1) {
                   ++*a_queries;
                   answers.push_back({.type = 1, .value = {127, 0, 0, 1}});
                } else if (question->type == 28) {
                   ++*aaaa_queries;
                   auto value = dns_fixture::bytes(16, 0);
                   value.back() = 1;
                   answers.push_back({.type = 28, .value = std::move(value)});
                }
                return dns_fixture::make_response(data, *question, answers);
             }};
         auto server = p2p::node{runtime, addressing_options("dual-stack-server", private_profile)};
         auto gate = std::make_shared<ipv4_dial_gate>();
         auto client_options = addressing_options("dual-stack-client", private_profile);
         client_options.connection_gater = gate;
         client_options.dns_resolver.nameservers = {{.address = "127.0.0.1", .port = dns.port()}};
         client_options.limits.resources.max_dial_attempts = 1;
         client_options.limits.resources.max_dial_attempts_per_peer = 1;
         client_options.direct_dial.max_concurrent_attempts = 2;
         auto client = p2p::node{runtime, std::move(client_options)};
         auto cleanup = std::unique_ptr<void, std::function<void(void*)>>{&client, [&](void*) noexcept {
            gate->release();
            client.request_stop();
            server.request_stop();
            for (auto* owner : {&client, &server}) {
               try {
                  forge::asio::blocking::run(runtime, owner->async_stop());
               } catch (...) {
                  BOOST_ERROR("dual-stack fixture failed to join node shutdown");
               }
            }
         }};
         server.register_protocol_handler(p2p::builtins::echo, echo_once);
         forge::asio::blocking::run(runtime, server.async_listen(p2p::parse_endpoint("/ip4/127.0.0.1/tcp/0")));
         const auto listening = server.local_endpoint();
         BOOST_REQUIRE(listening);
         auto stalled = stalled_ipv6_peer{runtime, listening->transport.port};
         const auto root = forge::multiformats::multiaddr::parse(
             "/dns/dual-stack.test/tcp/" + std::to_string(listening->transport.port) + "/p2p/" + server.local_peer().to_string());
         server.set_advertised_endpoints({p2p::parse_endpoint(root.to_string())});
         const auto started = std::chrono::steady_clock::now();
         auto connecting = asio::co_spawn(
             runtime.context(), client.async_connect(root, p2p::node::connect_options{
                 .expected_peer = server.local_peer(), .allow_relay = false, .timeout = 20s,
                 .direct_attempt_timeout = 15s, .max_direct_endpoints = 2, .allow_hole_punch = false}),
             asio::use_future);
         BOOST_REQUIRE(stalled.accepted.wait_until(started + 5s) == std::future_status::ready);
         stalled.accepted.get();
         BOOST_REQUIRE(gate->entered.wait_until(started + 5s) == std::future_status::ready);
         gate->entered.get();
         BOOST_TEST(gate->peer_calls.load() == 1U);
         BOOST_TEST(gate->ipv6_calls.load() == 1U);
         BOOST_TEST(gate->ipv4_calls.load() == 1U);
         BOOST_TEST(!gate->unexpected_address.load());
         const auto pending = client.diagnostics().resources;
         BOOST_TEST(pending.active_dials == 1U);
         BOOST_TEST(pending.system.outbound_connections == 1U);
         BOOST_TEST(pending.system.file_descriptors == 1U);
         BOOST_CHECK(connecting.wait_for(0s) == std::future_status::timeout);
         BOOST_CHECK(stalled.finished.wait_for(0s) == std::future_status::timeout);
         gate->release();
         BOOST_REQUIRE(connecting.wait_until(started + 8s) == std::future_status::ready);
         const auto session = connecting.get();
         const auto connected = client.diagnostics();
         BOOST_TEST(session.remote_peer.to_string() == server.local_peer().to_string());
         BOOST_TEST(connected.resources.active_dials == 0U);
         BOOST_TEST(connected.resources.transient.outbound_connections == 0U);
         BOOST_TEST(connected.resources.system.outbound_connections == 1U);
         BOOST_TEST(connected.resources.system.file_descriptors == 1U);
         BOOST_TEST(client.metrics().path_direct_attempts == 2U);
         BOOST_REQUIRE(stalled.finished.wait_until(started + 10s) == std::future_status::ready);
         BOOST_TEST(stalled.finished.get() > 0U);
         const auto winner = std::ranges::find_if(connected.sessions, [&](const auto& value) {
            return !value.closed && value.remote_peer == server.local_peer();
         });
         BOOST_REQUIRE(winner != connected.sessions.end());
         BOOST_REQUIRE(winner->remote_endpoint);
         BOOST_TEST(winner->remote_endpoint->transport.host == "127.0.0.1");
         auto stream = forge::asio::blocking::run(runtime, client.async_open_protocol_stream(
             server.local_peer(), p2p::builtins::echo, p2p::node::open_options{.allow_relay = false, .timeout = 3s,
                                                                          .allow_hole_punch = false}));
         const auto payload = std::vector<std::uint8_t>{'d', 'u', 'a', 'l'};
         forge::asio::blocking::run(runtime, stream.async_write_frame(payload));
         const auto echoed = forge::asio::blocking::run(runtime, stream.async_read_frame());
         BOOST_TEST(echoed == payload, boost::test_tools::per_element());
         BOOST_CHECK(std::chrono::steady_clock::now() - started < 12s);
         BOOST_TEST(client.metrics().path_direct_attempts == 3U);
         BOOST_TEST(client.metrics().handshakes_completed == 1U);
         BOOST_TEST(client.metrics().direct_failures == 0U);
         BOOST_TEST(gate->peer_calls.load() == 1U);
         BOOST_TEST(a_queries->load() > 0U);
         BOOST_TEST(aaaa_queries->load() > 0U);
         const auto peer = client.peers().find(server.local_peer());
         BOOST_REQUIRE(peer);
         BOOST_TEST(peer->failures == 0U);
         const auto source = std::ranges::find_if(peer->endpoints, [&](const auto& value) {
            return value.address.to_string() == root.to_string();
         });
         BOOST_REQUIRE(source != peer->endpoints.end());
         BOOST_TEST(source->successes >= 1U);
         BOOST_TEST(source->failures == 0U);
         BOOST_TEST(source->sources.learned);
         for (const auto& address : peer->endpoints) {
            if (address.address.to_string() != root.to_string()) {
               BOOST_TEST(!address.sources.learned);
               BOOST_TEST(address.successes == 0U);
               BOOST_TEST(address.failures == 0U);
            }
         }
         forge::asio::blocking::run(runtime, stream.async_close());
         forge::asio::blocking::run(runtime, client.async_stop());
         forge::asio::blocking::run(runtime, server.async_stop());
         BOOST_TEST(client.diagnostics().resources.active_dials == 0U);
         BOOST_TEST(client.diagnostics().resources.system.outbound_connections == 0U);
         BOOST_TEST(client.diagnostics().resources.system.file_descriptors == 0U);
         dns.close();
      }
   }
}

BOOST_AUTO_TEST_SUITE_END()
