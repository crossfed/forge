#include <boost/test/unit_test.hpp>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>

#include "../fixtures/local_dns_server.hxx"
#include "libp2p_identity_fixture.hxx"

import forge.asio.blocking;
import forge.asio.runtime;
import forge.exceptions;
import forge.multiformats.multiaddr;
import forge.net.dns.types;
import forge.net.p2p.diagnostics;
import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.lifecycle;
import forge.net.p2p.node;
import forge.net.p2p.peer_store;
import forge.net.p2p.private_network;
import forge.net.p2p.protocol;
import forge.net.pnet.protector;

namespace {
namespace p2p = forge::net::p2p;
namespace dns = forge::tests::dns;
using forge::multiformats::multiaddr;
using namespace std::chrono_literals;

class bootstrap_dns {
   struct state {
      std::mutex mutex;
      std::string target;
      std::string peer_suffix;
      std::string transport_suffix;
      bool silent = false;
      std::atomic_size_t first{0};
      std::atomic_size_t second{0};
      std::promise<void> queried;
   };
   std::shared_ptr<state> state_ = std::make_shared<state>();

 public:
   explicit bootstrap_dns(bool silent = false)
       : server([state = state_](const std::uint8_t* data, std::size_t size) -> std::optional<dns::bytes> {
            const auto question = dns::parse_question(data, size);
            if (!question) {
               return std::nullopt;
            }
            auto lock = std::scoped_lock{state->mutex};
            auto value = std::string{};
            if (question->name == "_dnsaddr.bootstrap.test" || question->name == "_dnsaddr.alias.test") {
               if (state->first.fetch_add(1) == 0) {
                  state->queried.set_value();
               }
               if (state->silent) {
                  return std::nullopt;
               }
               value = "dnsaddr=/dnsaddr/target.test";
               if (question->name == "_dnsaddr.alias.test") {
                  value += state->transport_suffix;
               }
               value += state->peer_suffix;
            } else if (question->name == "_dnsaddr.target.test") {
               ++state->second;
               value = "dnsaddr=" + state->target;
            } else {
               return dns::make_failure_response(data, *question, 3);
            }
            if (question->type != 16) {
               return dns::make_response(data, *question, {});
            }
            auto txt = dns::bytes{};
            for (auto offset = std::size_t{}; offset < value.size();) {
               const auto count = std::min(std::size_t{255}, value.size() - offset);
               txt.push_back(static_cast<std::uint8_t>(count));
               txt.insert(txt.end(), value.begin() + offset, value.begin() + offset + count);
               offset += count;
            }
            return dns::make_response(data, *question, {{.type = 16, .value = std::move(txt), .ttl = 0}});
         }), queried(state_->queried.get_future()) {
      const auto lock = std::scoped_lock{state_->mutex};
      state_->silent = silent;
   }

   void target(const p2p::endpoint& address) {
      const auto lock = std::scoped_lock{state_->mutex};
      state_->target = address.to_string();
      state_->peer_suffix = address.peer ? "/p2p/" + address.peer->to_string() : "";
      state_->transport_suffix = "/tcp/" + std::to_string(address.transport.port);
   }
   std::size_t first_queries() const { return state_->first.load(); }
   std::size_t second_queries() const { return state_->second.load(); }
   dns::local_dns_server server;
   std::future<void> queried;
};

p2p::node::options bootstrap_options(std::string name, bool private_profile = false) {
   auto identity = forge::tests::p2p::make_identity_fixture(std::move(name));
   auto options = p2p::node::options{};
   options.certificate_pem = std::move(identity.certificate_pem);
   options.private_key_pem = std::move(identity.private_key_pem);
   options.allow_insecure_test_mode = false;
   options.peer_state.persistence = p2p::peer_store::make_memory_persistence();
   options.lifecycle.startup_budget = 2s;
   options.lifecycle.connect_timeout = 1s;
   options.lifecycle.maintenance_interval = 25ms;
   options.lifecycle.bootstrap_retry_initial_delay = 25ms;
   options.lifecycle.bootstrap_retry_max_delay = 50ms;
   options.lifecycle.bootstrap_retry_jitter = 0.0;
   if (private_profile) {
      auto key = std::array<std::uint8_t, forge::net::pnet::pre_shared_key_size>{};
      for (auto i = std::size_t{}; i < key.size(); ++i) {
         key[i] = static_cast<std::uint8_t>(i);
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

template <typename T> T await_ready(std::future<T>& future) {
   BOOST_REQUIRE(future.wait_for(4s) == std::future_status::ready);
   return future.get();
}

template <typename Predicate> bool eventually(Predicate predicate) {
   const auto deadline = std::chrono::steady_clock::now() + 4s;
   while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
         return false;
      }
      std::this_thread::sleep_for(5ms);
   }
   return true;
}

class node_cleanup {
 public:
   explicit node_cleanup(forge::asio::runtime& runtime) : runtime_(runtime) {}
   ~node_cleanup() {
      for (auto* node : nodes) {
         node->request_stop();
      }
      for (auto* node : nodes) {
         try {
            auto stopped = boost::asio::co_spawn(runtime_.context(), node->async_stop(), boost::asio::use_future);
            if (stopped.wait_for(4s) != std::future_status::ready) {
               BOOST_ERROR("bootstrap fixture shutdown did not join");
               runtime_.stop();
               return;
            }
            stopped.get();
         } catch (...) {
            BOOST_ERROR("bootstrap fixture shutdown failed");
         }
      }
   }
   std::vector<p2p::node*> nodes;
 private:
   forge::asio::runtime& runtime_;
};
} // namespace

BOOST_AUTO_TEST_SUITE(node_bootstrap_addressing_tests)

BOOST_AUTO_TEST_CASE(p2p_bootstrap_dnsaddr_two_hops_preserve_root_native_and_private) {
   for (const auto private_profile : {false, true}) {
      BOOST_TEST_CONTEXT("private=" << private_profile) {
         auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
         auto resolver = bootstrap_dns{};
         auto server = p2p::node{runtime, bootstrap_options("bootstrap-dns-server", private_profile)};
         forge::asio::blocking::run(runtime, server.async_listen(p2p::parse_endpoint("/ip4/127.0.0.1/tcp/0")));
         BOOST_REQUIRE(server.local_endpoint());
         resolver.target(*server.local_endpoint());
         const auto root = multiaddr::parse("/dnsaddr/bootstrap.test/p2p/" + server.local_peer().to_string());
         auto options = bootstrap_options("bootstrap-dns-client", private_profile);
         options.dns_resolver.nameservers = {{.address = "127.0.0.1", .port = resolver.server.port()}};
         options.lifecycle.bootstrap = {{.address = root}};
         options.lifecycle.requirement = p2p::bootstrap_requirement::require_connection;
         options.limits.resources.max_dial_attempts = 1;
         auto client = p2p::node{runtime, std::move(options)};
         auto cleanup = node_cleanup{runtime};
         cleanup.nodes = {&client, &server};
         auto starting = boost::asio::co_spawn(runtime.context(), client.async_start(), boost::asio::use_future);
         const auto status = await_ready(starting);
         BOOST_TEST(status.connected_bootstrap == 1U);
         BOOST_TEST(!status.degraded);
         BOOST_TEST(client.is_peer_protected(server.local_peer()));
         BOOST_TEST(resolver.first_queries() >= 1U);
         BOOST_TEST(resolver.second_queries() >= 1U);
         BOOST_TEST(client.diagnostics().resources.active_dials == 0U);
         const auto record = client.peers().find(server.local_peer());
         BOOST_REQUIRE(record);
         const auto found = std::ranges::find_if(record->endpoints, [&](const auto& entry) {
            return entry.address.to_string() == root.to_string();
         });
         BOOST_REQUIRE(found != record->endpoints.end());
         BOOST_TEST(found->sources.learned);
         BOOST_TEST(found->successes >= 1U);
         for (const auto& entry : record->endpoints) {
            if (entry.address.to_string() != root.to_string()) {
               BOOST_TEST(!entry.sources.learned);
               BOOST_TEST(entry.successes == 0U);
            }
         }
         for (auto invalid : {std::vector<p2p::bootstrap_peer>{{.address = root}, {}},
                              std::vector<p2p::bootstrap_peer>(4097, p2p::bootstrap_peer{.address = root}),
                              std::vector<p2p::bootstrap_peer>{{.address = multiaddr::parse("/dnsaddr/bootstrap.test/ws")}}}) {
            BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, client.async_set_bootstrap(std::move(invalid))),
                              p2p::exceptions::invalid_options);
            BOOST_TEST(client.lifecycle_state().configured_bootstrap == 1U);
            BOOST_TEST(client.is_peer_protected(server.local_peer()));
         }
         if (private_profile) {
            for (const auto& text : {"/dnsaddr/bootstrap.test/udp/4001/quic-v1",
                                     "/dnsaddr/bootstrap.test/p2p-circuit"}) {
               BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, client.async_set_bootstrap(
                                     {{.address = multiaddr::parse(text)}})), p2p::exceptions::invalid_options);
               BOOST_TEST(client.lifecycle_state().configured_bootstrap == 1U);
               BOOST_TEST(client.is_peer_protected(server.local_peer()));
            }
         }
         forge::asio::blocking::run(runtime, client.async_set_bootstrap({}));
         BOOST_TEST(!client.is_peer_protected(server.local_peer()));
         resolver.server.close();
      }
   }
}

BOOST_AUTO_TEST_CASE(p2p_bootstrap_dnsaddr_expected_peer_mismatch_is_not_learned) {
   for (const auto private_profile : {false, true}) {
      auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
      auto resolver = bootstrap_dns{};
      auto server = p2p::node{runtime, bootstrap_options("bootstrap-mismatch-server", private_profile)};
      forge::asio::blocking::run(runtime, server.async_listen(p2p::parse_endpoint("/ip4/127.0.0.1/tcp/0")));
      resolver.target(*server.local_endpoint());
      auto options = bootstrap_options("bootstrap-mismatch-client", private_profile);
      const auto wrong = p2p::make_peer_id_from_certificate_pem(options.certificate_pem);
      options.dns_resolver.nameservers = {{.address = "127.0.0.1", .port = resolver.server.port()}};
      options.lifecycle.bootstrap = {{.address = multiaddr::parse("/dnsaddr/bootstrap.test/p2p/" + wrong.to_string())}};
      auto client = p2p::node{runtime, std::move(options)};
      auto cleanup = node_cleanup{runtime};
      cleanup.nodes = {&client, &server};
      auto starting = boost::asio::co_spawn(runtime.context(), client.async_start(), boost::asio::use_future);
      const auto status = await_ready(starting);
      BOOST_TEST(status.connected_bootstrap == 0U);
      BOOST_TEST(status.degraded);
      BOOST_TEST(!client.is_peer_protected(server.local_peer()));
      BOOST_TEST(!client.peers().find(server.local_peer()));
      BOOST_TEST(client.metrics().path_direct_attempts == 0U);
      forge::asio::blocking::run(runtime, client.async_set_bootstrap({}));
      resolver.server.close();
   }
}

BOOST_AUTO_TEST_CASE(p2p_bootstrap_suffixless_dnsaddr_rotates_peer_and_preserves_alias_protection) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto resolver = bootstrap_dns{};
   auto first = p2p::node{runtime, bootstrap_options("bootstrap-rotation-first")};
   auto second = p2p::node{runtime, bootstrap_options("bootstrap-rotation-second")};
   for (auto* node : {&first, &second}) {
      forge::asio::blocking::run(runtime, node->async_listen(p2p::parse_endpoint("/ip4/127.0.0.1/tcp/0")));
   }
   resolver.target(*first.local_endpoint());
   const auto root = multiaddr::parse("/dnsaddr/bootstrap.test");
   const auto alias = multiaddr::parse("/dnsaddr/alias.test");
   auto options = bootstrap_options("bootstrap-rotation-client");
   options.dns_resolver.nameservers = {{.address = "127.0.0.1", .port = resolver.server.port()}};
   options.lifecycle.bootstrap = {{.address = root}, {.address = alias}};
   auto client = p2p::node{runtime, std::move(options)};
   auto cleanup = node_cleanup{runtime};
   cleanup.nodes = {&client, &second, &first};
   auto starting = boost::asio::co_spawn(runtime.context(), client.async_start(), boost::asio::use_future);
   BOOST_TEST(await_ready(starting).connected_bootstrap == 2U);
   BOOST_TEST(client.is_peer_protected(first.local_peer()));
   forge::asio::blocking::run(runtime, client.async_set_bootstrap({{.address = root}}));
   BOOST_TEST(client.is_peer_protected(first.local_peer()));
   const auto queries = resolver.second_queries();
   resolver.target(*second.local_endpoint());
   forge::asio::blocking::run(runtime, first.async_stop());
   BOOST_REQUIRE(eventually([&] { return client.is_peer_protected(second.local_peer()); }));
   BOOST_TEST(!client.is_peer_protected(first.local_peer()));
   BOOST_TEST(resolver.second_queries() > queries);
   const auto record = client.peers().find(second.local_peer());
   BOOST_REQUIRE(record);
   BOOST_CHECK(std::ranges::any_of(record->endpoints, [&](const auto& item) {
      return item.address.to_string() == root.to_string() && item.sources.learned && item.successes > 0;
   }));
   forge::asio::blocking::run(runtime, client.async_set_bootstrap({}));
   BOOST_TEST(!client.is_peer_protected(second.local_peer()));
   resolver.server.close();
}

BOOST_AUTO_TEST_CASE(p2p_bootstrap_removal_or_stop_during_dns_drains_logical_dial) {
   for (const auto remove : {false, true}) {
      auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
      auto resolver = bootstrap_dns{true};
      auto options = bootstrap_options("bootstrap-cancel-client");
      options.dns_resolver.nameservers = {{.address = "127.0.0.1", .port = resolver.server.port()}};
      options.lifecycle.bootstrap = {{.address = multiaddr::parse("/dnsaddr/bootstrap.test")}};
      options.lifecycle.startup_budget = 10s;
      options.lifecycle.connect_timeout = 10s;
      auto client = p2p::node{runtime, std::move(options)};
      auto cleanup = node_cleanup{runtime};
      cleanup.nodes = {&client};
      auto starting = boost::asio::co_spawn(runtime.context(), client.async_start(), boost::asio::use_future);
      await_ready(resolver.queried);
      BOOST_TEST(client.diagnostics().resources.active_dials == 1U);
      if (remove) {
         forge::asio::blocking::run(runtime, client.async_set_bootstrap({}));
         static_cast<void>(await_ready(starting));
         BOOST_TEST(client.lifecycle_state().configured_bootstrap == 0U);
      }
      auto stopping = boost::asio::co_spawn(runtime.context(), client.async_stop(), boost::asio::use_future);
      await_ready(stopping);
      if (!remove) {
         BOOST_REQUIRE(starting.wait_for(1s) == std::future_status::ready);
         BOOST_CHECK_THROW(starting.get(), forge::exceptions::base);
      }
      BOOST_TEST(client.diagnostics().resources.active_dials == 0U);
      BOOST_TEST(client.diagnostics().resources.system.outbound_connections == 0U);
      BOOST_TEST(client.diagnostics().resources.system.file_descriptors == 0U);
      BOOST_TEST(client.metrics().path_direct_attempts == 0U);
      resolver.server.close();
   }
}

BOOST_AUTO_TEST_CASE(p2p_bootstrap_alias_requires_its_own_root_proof_without_repeated_dials) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 4}};
   auto resolver = bootstrap_dns{};
   auto server = p2p::node{runtime, bootstrap_options("bootstrap-root-proof-server")};
   forge::asio::blocking::run(runtime, server.async_listen(p2p::parse_endpoint("/ip4/127.0.0.1/tcp/0")));
   resolver.target(*server.local_endpoint());
   const auto suffix = "/p2p/" + server.local_peer().to_string();
   const auto root = multiaddr::parse("/dnsaddr/bootstrap.test" + suffix);
   const auto alias = multiaddr::parse("/dnsaddr/alias.test/tcp/" +
                                     std::to_string(server.local_endpoint()->transport.port) + suffix);
   auto options = bootstrap_options("bootstrap-root-proof-client");
   options.dns_resolver.nameservers = {{.address = "127.0.0.1", .port = resolver.server.port()}};
   options.lifecycle.bootstrap = {{.address = root}, {.address = alias}};
   auto client = p2p::node{runtime, std::move(options)};
   auto cleanup = node_cleanup{runtime};
   cleanup.nodes = {&client, &server};
   // An unrelated numeric session for the configured peer is not DNS root proof.
   static_cast<void>(forge::asio::blocking::run(runtime, client.async_connect(*server.local_endpoint())));
   BOOST_TEST(client.lifecycle_state().connected_bootstrap == 0U);
   const auto attempts = client.metrics().path_direct_attempts;
   auto starting = boost::asio::co_spawn(runtime.context(), client.async_start(), boost::asio::use_future);
   BOOST_TEST(await_ready(starting).connected_bootstrap == 2U);
   BOOST_TEST(client.metrics().path_direct_attempts == attempts + 2U);
   const auto queries = resolver.first_queries();
   // Give the production maintenance loop several opportunities to inspect
   // the two established root-specific sessions, without forcing another dial.
   std::this_thread::sleep_for(150ms);
   BOOST_TEST(resolver.first_queries() == queries);
   BOOST_TEST(client.metrics().path_direct_attempts == attempts + 2U);
   forge::asio::blocking::run(runtime, client.async_set_bootstrap({{.address = alias}}));
   BOOST_TEST(client.is_peer_protected(server.local_peer()));
   BOOST_TEST(client.lifecycle_state().connected_bootstrap == 1U);
   forge::asio::blocking::run(runtime, client.async_set_bootstrap({}));
   BOOST_TEST(!client.is_peer_protected(server.local_peer()));
   resolver.server.close();
}

BOOST_AUTO_TEST_SUITE_END()
