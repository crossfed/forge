module;

#include <boost/asio/awaitable.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../quic_p2p/libp2p_identity_fixture.hxx"

module forge.plugins.p2p.node.plugin;

import forge.app.application_builder;
import forge.app.application_shell;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.config.core.document;
import forge.config.core.value;
import forge.db.core.driver;
import forge.net.p2p.dht;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.peer_store;
import forge.net.p2p.rendezvous;
import forge.plugins.crypto.secrets.plugin;
import forge.plugins.db.store.api;
import forge.plugins.db.store.plugin;

#include "details/object_peer_state_adapter.hxx"

namespace {

namespace p2p = forge::net::p2p;
namespace p2p_node = forge::plugins::p2p::node;
namespace secrets_plugin = forge::plugins::crypto::secrets;
namespace store_plugin = forge::plugins::db::store;

constexpr auto peer_store_name = std::string_view{"p2p-peer-state"};

struct root_guard {
   std::filesystem::path root =
       std::filesystem::temp_directory_path() /
       ("forge_p2p_peer_state_tests_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

   root_guard() {
      std::filesystem::remove_all(root);
   }

   ~root_guard() {
      std::filesystem::remove_all(root);
   }
};

[[nodiscard]] forge::config::core::value configured_store(std::string driver, const std::filesystem::path& path) {
   auto object_layer = forge::config::core::value::object_type{};
   object_layer.emplace("family", forge::config::core::value{std::string{"objectdb"}});
   object_layer.emplace("write-policy", forge::config::core::value{std::string{"single-writer"}});

   auto store = forge::config::core::value::object_type{};
   store.emplace("name", forge::config::core::value{std::string{peer_store_name}});
   store.emplace("driver", forge::config::core::value{std::move(driver)});
   store.emplace("path", forge::config::core::value{path.string()});
   store.emplace("object", forge::config::core::value{std::move(object_layer)});
   return forge::config::core::value{std::move(store)};
}

[[nodiscard]] forge::config::core::document document_for(std::string driver, const std::filesystem::path& path) {
   auto document = forge::config::core::document{};
   document.set("plugins.db.store.stores", forge::config::core::value::array_type{
                                               configured_store(std::move(driver), path),
                                           });
   return document;
}

[[nodiscard]] forge::config::core::value secret(std::string id, std::string_view value, std::string purpose) {
   auto source = forge::config::core::value::object_type{};
   source.emplace("type", forge::config::core::value{"value"});
   source.emplace("encoding", forge::config::core::value{"raw"});
   source.emplace("value", forge::config::core::value{std::string{value}});

   auto configured = forge::config::core::value::object_type{};
   configured.emplace("id", forge::config::core::value{std::move(id)});
   configured.emplace("kind", forge::config::core::value{"bytes"});
   configured.emplace("source", forge::config::core::value{std::move(source)});
   configured.emplace("purposes", forge::config::core::value::array_type{
                                      forge::config::core::value{std::move(purpose)},
                                  });
   configured.emplace("operations", forge::config::core::value::array_type{
                                        forge::config::core::value{"get_bytes"},
                                    });
   configured.emplace("allow-raw-export", forge::config::core::value{true});
   return forge::config::core::value{std::move(configured)};
}

[[nodiscard]] forge::config::core::document p2p_document_for(std::string driver, const std::filesystem::path& path,
                                                             const forge::tests::p2p::identity_fixture& identity) {
   constexpr auto certificate_id = std::string_view{"p2p/test-certificate"};
   constexpr auto private_key_id = std::string_view{"p2p/test-private-key"};
   auto document = document_for(std::move(driver), path);
   document.set("plugins.crypto.secrets.secrets",
                forge::config::core::value::array_type{
                    secret(std::string{certificate_id}, identity.certificate_pem, "p2p.identity.certificate"),
                    secret(std::string{private_key_id}, identity.private_key_pem, "p2p.identity.private-key"),
                });
   document.set("plugins.p2p.node.listen", forge::config::core::value::array_type{
                                               forge::config::core::value{"/ip4/127.0.0.1/udp/0/quic-v1"},
                                           });
   document.set("plugins.p2p.node.peer-store.store", std::string{peer_store_name});
   document.set("plugins.p2p.node.identity.certificate-secret", std::string{certificate_id});
   document.set("plugins.p2p.node.identity.private-key-secret", std::string{private_key_id});
   return document;
}

[[nodiscard]] std::unique_ptr<forge::app::application_shell> make_app(forge::config::core::document document) {
   auto builder = forge::app::application_builder{};
   builder.name("p2p-peer-state-persistence-test")
       .runtime(forge::asio::runtime_options{.worker_threads = 1, .thread_name = "p2p-peer-state-test"})
       .plugin(store_plugin::descriptor());
   auto app = std::move(builder).build();
   app->configure(document);
   forge::asio::blocking::run(app->runtime(), app->startup());
   return app;
}

[[nodiscard]] std::unique_ptr<forge::app::application_shell> build_p2p_app(forge::config::core::document document) {
   auto builder = forge::app::application_builder{};
   builder.name("p2p-peer-state-maintenance-test")
       .runtime(forge::asio::runtime_options{.worker_threads = 1, .thread_name = "p2p-maintenance-test"})
       .plugin(store_plugin::descriptor())
       .plugin(secrets_plugin::descriptor())
       .plugin(p2p_node::descriptor());
   auto app = std::move(builder).build();
   app->configure(document);
   return app;
}

[[nodiscard]] std::unique_ptr<forge::app::application_shell> make_p2p_app(forge::config::core::document document) {
   auto app = build_p2p_app(std::move(document));
   forge::asio::blocking::run(app->runtime(), app->startup());
   return app;
}

class tracking_store_api final : public store_plugin::api {
 public:
   explicit tracking_store_api(store_plugin::api* delegate) : delegate_{delegate} {}

   boost::asio::awaitable<void> add_store(std::string name, std::shared_ptr<forge::db::core::driver> driver,
                                          store_plugin::store_options options) override {
      co_await delegate_->add_store(std::move(name), std::move(driver), std::move(options));
   }

   boost::asio::awaitable<store_plugin::store_handle> store(std::string name) override {
      co_return co_await delegate_->store(std::move(name));
   }

   boost::asio::awaitable<void> flush(std::string name, bool sync) override {
      ++flush_calls;
      last_store = std::move(name);
      last_sync = sync;
      co_await delegate_->flush(last_store, sync);
   }

   boost::asio::awaitable<void> flush_all(bool sync) override {
      co_await delegate_->flush_all(sync);
   }

   boost::asio::awaitable<store_plugin::status> status() override {
      co_return co_await delegate_->status();
   }

   std::size_t flush_calls = 0;
   std::string last_store;
   bool last_sync = false;

 private:
   store_plugin::api* delegate_;
};

[[nodiscard]] std::shared_ptr<p2p::peer_store::persistence> open_peer_persistence(forge::app::application_shell& app,
                                                                                  bool register_schema = true) {
   auto stores = app.apis().get<store_plugin::api>(store_plugin::api::ref());
   auto handle = forge::asio::blocking::run(app.runtime(), stores->store(std::string{peer_store_name}));
   if (register_schema) {
      p2p_node::object_peer_state_adapter::register_schema(handle);
   }
   return forge::asio::blocking::run(
       app.runtime(), p2p_node::object_peer_state_adapter::async_open(stores.operator->(), std::move(handle)));
}

[[nodiscard]] p2p::peer_store open_peer_state(forge::app::application_shell& app) {
   return p2p::peer_store{p2p::peer_store::options{
       .persistence = open_peer_persistence(app),
       .max_peers = 1,
       .max_pending = 4,
   }};
}

template <typename DocumentFactory> void check_object_peer_state_persistence(DocumentFactory&& make_document) {
   const auto low = p2p::make_peer_id_from_certificate_pem(
       forge::tests::p2p::make_identity_fixture("p2p-peer-state-low").certificate_pem);
   const auto high = p2p::make_peer_id_from_certificate_pem(
       forge::tests::p2p::make_identity_fixture("p2p-peer-state-high").certificate_pem);
   const auto provider = p2p::make_peer_id_from_certificate_pem(
       forge::tests::p2p::make_identity_fixture("p2p-peer-state-provider").certificate_pem);
   const auto key = p2p::make_dht_key(
       std::vector<std::uint8_t>{'p', '2', 'p', '-', 'p', 'e', 'e', 'r', '-', 's', 't', 'a', 't', 'e'});
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};
   auto highest_sequence = std::uint64_t{};

   {
      auto app = make_app(make_document());
      auto state = open_peer_state(*app);
      state.upsert(p2p::peer_store::record{
          .peer = low,
          .protocol_version = "/forge/persisted/low/1",
          .discovery_expires_at = expires_at,
          .failures = 10,
      });
      state.upsert(p2p::peer_store::record{
          .peer = high,
          .protocol_version = "/forge/persisted/high/1",
          .discovery_expires_at = expires_at,
          .successes = 10,
      });
      forge::asio::blocking::run(app->runtime(), state.async_flush());
      forge::asio::blocking::run(app->runtime(), state.async_upsert_provider(p2p::peer_store::provider_record{
                                                     .key = key,
                                                     .provider = p2p::dht::peer{.id = provider},
                                                     .expires_at = expires_at,
                                                 }));
      forge::asio::blocking::run(app->runtime(), state.async_upsert_rendezvous(p2p::rendezvous::registration{
                                                     .namespace_name = "forge.peer-state",
                                                     .peer = low,
                                                     .ttl = std::chrono::hours{1},
                                                     .expires_at = expires_at,
                                                 }));
      forge::asio::blocking::run(app->runtime(), state.async_upsert_rendezvous(p2p::rendezvous::registration{
                                                     .namespace_name = "forge.peer-state",
                                                     .peer = high,
                                                     .ttl = std::chrono::hours{1},
                                                     .expires_at = expires_at,
                                                 }));
      const auto registrations = state.discover_rendezvous("forge.peer-state", 0, 10);
      BOOST_REQUIRE_EQUAL(registrations.size(), 2U);
      highest_sequence = registrations.back().sequence;
      forge::asio::blocking::run(app->runtime(), state.async_remove_rendezvous(low, "forge.peer-state"));
      forge::asio::blocking::run(app->runtime(), state.async_remove_rendezvous(high, "forge.peer-state"));
      forge::asio::blocking::run(app->runtime(), state.async_close());
      forge::asio::blocking::run(app->runtime(), app->shutdown());
   }

   {
      auto app = make_app(make_document());
      auto state = open_peer_state(*app);
      forge::asio::blocking::run(app->runtime(), state.async_hydrate());
      BOOST_REQUIRE_EQUAL(state.snapshot(10).size(), 1U);
      BOOST_TEST(!state.find(low).has_value());
      BOOST_REQUIRE(state.find(high).has_value());
      const auto providers = state.find_providers(key, 10);
      BOOST_REQUIRE_EQUAL(providers.size(), 1U);
      BOOST_TEST(providers.front().provider.id.value == provider.value);
      BOOST_TEST(state.discover_rendezvous("forge.peer-state", 0, 10).empty());

      forge::asio::blocking::run(app->runtime(), state.async_upsert_rendezvous(p2p::rendezvous::registration{
                                                     .namespace_name = "forge.peer-state",
                                                     .peer = provider,
                                                     .ttl = std::chrono::hours{1},
                                                     .expires_at = expires_at,
                                                 }));
      const auto added = state.discover_rendezvous("forge.peer-state", highest_sequence, 10);
      BOOST_REQUIRE_EQUAL(added.size(), 1U);
      BOOST_TEST(added.front().sequence > highest_sequence);
      forge::asio::blocking::run(app->runtime(), state.async_close());
      forge::asio::blocking::run(app->runtime(), app->shutdown());
   }
}

void check_plugin_maintenance_prunes_expired_peer(std::string driver, const std::filesystem::path& path) {
   const auto identity = forge::tests::p2p::make_identity_fixture("p2p-maintenance-node");
   const auto expired = p2p::make_peer_id_from_certificate_pem(
       forge::tests::p2p::make_identity_fixture("p2p-maintenance-expired").certificate_pem);
   auto app = make_p2p_app(p2p_document_for(std::move(driver), path, identity));
   auto persistence = open_peer_persistence(*app, false);
   auto state = p2p::peer_store{p2p::peer_store::options{.persistence = persistence}};
   state.upsert(p2p::peer_store::record{
       .peer = expired,
       .protocol_version = "/forge/expired/1",
       .discovery_expires_at = std::chrono::system_clock::now() - std::chrono::seconds{1},
   });
   forge::asio::blocking::run(app->runtime(), state.async_flush());

   const auto contains_expired = [&](const p2p::peer_store::hydration_page& page) {
      return std::ranges::any_of(page.peers, [&](const auto& value) { return value.peer == expired; });
   };
   auto page = forge::asio::blocking::run(app->runtime(), persistence->async_hydrate(p2p::peer_store::hydration_request{
                                                              .kind = p2p::peer_store::hydration_kind::peers,
                                                              .limit = 256,
                                                          }));
   BOOST_REQUIRE(contains_expired(page));

   for (auto attempt = 0; attempt < 80 && contains_expired(page); ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds{50});
      page = forge::asio::blocking::run(app->runtime(), persistence->async_hydrate(p2p::peer_store::hydration_request{
                                                            .kind = p2p::peer_store::hydration_kind::peers,
                                                            .limit = 256,
                                                        }));
   }
   BOOST_TEST(!contains_expired(page));
   forge::asio::blocking::run(app->runtime(), state.async_close());
   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

void check_object_peer_state_prune_rotates_categories(std::string driver, const std::filesystem::path& path) {
   auto app = make_app(document_for(std::move(driver), path));
   auto persistence = open_peer_persistence(*app);
   auto state = p2p::peer_store{p2p::peer_store::options{
       .persistence = persistence,
       .prune_page_limit = 1,
   }};
   const auto expired_at = std::chrono::system_clock::now() - std::chrono::seconds{1};
   const auto peer = p2p::make_peer_id_from_certificate_pem(
       forge::tests::p2p::make_identity_fixture("p2p-prune-peer").certificate_pem);
   const auto provider = p2p::make_peer_id_from_certificate_pem(
       forge::tests::p2p::make_identity_fixture("p2p-prune-provider").certificate_pem);
   const auto registration = p2p::make_peer_id_from_certificate_pem(
       forge::tests::p2p::make_identity_fixture("p2p-prune-rendezvous").certificate_pem);
   const auto key = p2p::make_dht_key(std::vector<std::uint8_t>{'p', 'r', 'u', 'n', 'e'});

   state.upsert(p2p::peer_store::record{.peer = peer, .discovery_expires_at = expired_at});
   forge::asio::blocking::run(app->runtime(), state.async_flush());
   forge::asio::blocking::run(app->runtime(), state.async_upsert_provider(p2p::peer_store::provider_record{
                                                  .key = key,
                                                  .provider = p2p::dht::peer{.id = provider},
                                                  .expires_at = expired_at,
                                              }));
   forge::asio::blocking::run(app->runtime(), state.async_upsert_rendezvous(p2p::rendezvous::registration{
                                                  .namespace_name = "forge.prune.rotation",
                                                  .peer = registration,
                                                  .ttl = std::chrono::seconds{1},
                                                  .expires_at = expired_at,
                                              }));

   const auto first = forge::asio::blocking::run(app->runtime(), state.async_prune_expired());
   const auto second = forge::asio::blocking::run(app->runtime(), state.async_prune_expired());
   const auto third = forge::asio::blocking::run(app->runtime(), state.async_prune_expired());

   BOOST_TEST(first.peers == 1U);
   BOOST_TEST(second.providers == 1U);
   BOOST_TEST(third.rendezvous_registrations == 1U);
   BOOST_TEST(!third.may_have_more);
   BOOST_TEST(state.snapshot(1).empty());
   BOOST_TEST(state.find_providers(key, 1).empty());
   BOOST_TEST(state.discover_rendezvous("forge.prune.rotation", 0, 1).empty());

   forge::asio::blocking::run(app->runtime(), state.async_close());
   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

void check_object_peer_state_durable_acknowledgement(std::string driver, const std::filesystem::path& path) {
   auto app = make_app(document_for(std::move(driver), path));
   auto stores = app->apis().get<store_plugin::api>(store_plugin::api::ref());
   auto handle = forge::asio::blocking::run(app->runtime(), stores->store(std::string{peer_store_name}));
   p2p_node::object_peer_state_adapter::register_schema(handle);
   auto tracking = tracking_store_api{stores.operator->()};
   auto persistence = forge::asio::blocking::run(
       app->runtime(), p2p_node::object_peer_state_adapter::async_open(&tracking, std::move(handle)));
   auto state = p2p::peer_store{p2p::peer_store::options{.persistence = persistence}};
   const auto provider = p2p::make_peer_id_from_certificate_pem(
       forge::tests::p2p::make_identity_fixture("p2p-durable-provider").certificate_pem);
   const auto registration = p2p::make_peer_id_from_certificate_pem(
       forge::tests::p2p::make_identity_fixture("p2p-durable-rendezvous").certificate_pem);
   const auto key = p2p::make_dht_key(std::vector<std::uint8_t>{'d', 'u', 'r', 'a', 'b', 'l', 'e'});
   const auto expires_at = std::chrono::system_clock::now() + std::chrono::hours{1};

   forge::asio::blocking::run(app->runtime(), state.async_upsert_provider(p2p::peer_store::provider_record{
                                                  .key = key,
                                                  .provider = p2p::dht::peer{.id = provider},
                                                  .expires_at = expires_at,
                                              }));
   BOOST_TEST(tracking.flush_calls == 1U);
   BOOST_TEST(tracking.last_store == peer_store_name);
   BOOST_TEST(tracking.last_sync);

   forge::asio::blocking::run(app->runtime(), state.async_upsert_rendezvous(p2p::rendezvous::registration{
                                                  .namespace_name = "forge.durable",
                                                  .peer = registration,
                                                  .ttl = std::chrono::hours{1},
                                                  .expires_at = expires_at,
                                              }));
   BOOST_TEST(tracking.flush_calls == 2U);
   BOOST_TEST(tracking.last_sync);

   (void)forge::asio::blocking::run(app->runtime(), state.async_prune_expired());
   BOOST_TEST(tracking.flush_calls == 2U);
   forge::asio::blocking::run(app->runtime(), state.async_close());
   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

void check_plugin_startup_rolls_back_open_peer_state(std::string driver, const std::filesystem::path& path) {
   const auto identity = forge::tests::p2p::make_identity_fixture("p2p-startup-rollback");
   const auto foreign = forge::tests::p2p::make_identity_fixture("p2p-startup-rollback-foreign");
   auto document = p2p_document_for(driver, path, identity);
   document.set("plugins.p2p.node.peer-id",
                p2p::make_peer_id_from_certificate_pem(foreign.certificate_pem).to_string());

   {
      auto app = build_p2p_app(std::move(document));
      BOOST_CHECK_THROW(forge::asio::blocking::run(app->runtime(), app->startup()), p2p::exceptions::invalid_identity);
   }

   auto reopened = make_app(document_for(std::move(driver), path));
   auto persistence = open_peer_persistence(*reopened);
   forge::asio::blocking::run(reopened->runtime(), persistence->async_close());
   forge::asio::blocking::run(reopened->runtime(), reopened->shutdown());
}

} // namespace

BOOST_AUTO_TEST_SUITE(p2p_peer_state_persistence_test_suite)

#if FORGE_HAS_MDBX
BOOST_AUTO_TEST_CASE(p2p_peer_state_mdbx_preserves_eviction_provider_and_sequence_state) {
   auto root = root_guard{};
   const auto path = root.root / "mdbx";
   check_object_peer_state_persistence([&] { return document_for("mdbx", path); });
}

BOOST_AUTO_TEST_CASE(p2p_plugin_mdbx_maintenance_prunes_expired_peer_state) {
   auto root = root_guard{};
   check_plugin_maintenance_prunes_expired_peer("mdbx", root.root / "mdbx-maintenance");
}

BOOST_AUTO_TEST_CASE(p2p_peer_state_mdbx_prune_rotates_bounded_categories) {
   auto root = root_guard{};
   check_object_peer_state_prune_rotates_categories("mdbx", root.root / "mdbx-prune-rotation");
}

BOOST_AUTO_TEST_CASE(p2p_peer_state_mdbx_durable_operations_flush_before_acknowledgement) {
   auto root = root_guard{};
   check_object_peer_state_durable_acknowledgement("mdbx", root.root / "mdbx-durable-ack");
}

BOOST_AUTO_TEST_CASE(p2p_plugin_mdbx_startup_failure_rolls_back_open_peer_state) {
   auto root = root_guard{};
   check_plugin_startup_rolls_back_open_peer_state("mdbx", root.root / "mdbx-startup-rollback");
}
#endif

#if FORGE_HAS_ROCKSDB
BOOST_AUTO_TEST_CASE(p2p_peer_state_rocksdb_preserves_eviction_provider_and_sequence_state) {
   auto root = root_guard{};
   const auto path = root.root / "rocksdb";
   check_object_peer_state_persistence([&] { return document_for("rocksdb", path); });
}

BOOST_AUTO_TEST_CASE(p2p_plugin_rocksdb_maintenance_prunes_expired_peer_state) {
   auto root = root_guard{};
   check_plugin_maintenance_prunes_expired_peer("rocksdb", root.root / "rocksdb-maintenance");
}

BOOST_AUTO_TEST_CASE(p2p_peer_state_rocksdb_prune_rotates_bounded_categories) {
   auto root = root_guard{};
   check_object_peer_state_prune_rotates_categories("rocksdb", root.root / "rocksdb-prune-rotation");
}

BOOST_AUTO_TEST_CASE(p2p_peer_state_rocksdb_durable_operations_flush_before_acknowledgement) {
   auto root = root_guard{};
   check_object_peer_state_durable_acknowledgement("rocksdb", root.root / "rocksdb-durable-ack");
}

BOOST_AUTO_TEST_CASE(p2p_plugin_rocksdb_startup_failure_rolls_back_open_peer_state) {
   auto root = root_guard{};
   check_plugin_startup_rolls_back_open_peer_state("rocksdb", root.root / "rocksdb-startup-rollback");
}
#endif

BOOST_AUTO_TEST_SUITE_END()
