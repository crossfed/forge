#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/exceptions/macros.hpp>
#include <forge/objectdb/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

import forge.api.binding;
import forge.api.registry;
import forge.app.application_builder;
import forge.app.application_shell;
import forge.app.events;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.app.signals;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.asio.task_scheduler;
import forge.config.component;
import forge.config.document;
import forge.config.value;
import forge.ids.object_id;
import forge.objectdb.cursor;
import forge.objectdb.driver;
import forge.objectdb.exceptions;
import forge.objectdb.index;
import forge.objectdb.object;
import forge.objectdb.record;
import forge.objectdb.session;
import forge.objectdb.store;
import forge.plugins.db.objectdb.api;
import forge.plugins.db.objectdb.exceptions;
import forge.plugins.db.objectdb.plugin;
import forge.plugins.db.objectdb.types;

#if FORGE_HAS_ROCKSDB
import forge.objectdb.rocksdb;
#endif

namespace {

namespace objectdb_plugin = forge::plugins::db::objectdb;

struct by_id;
struct by_name;

struct account : forge::objectdb::object<account, 1, 7> {
   std::string name;
   std::uint64_t balance = 0;

   bool operator==(const account&) const = default;
};

BOOST_DESCRIBE_STRUCT(account, (forge::objectdb::object<account, 1, 7>), (name, balance))

using account_object =
   forge::objectdb::object_index<account,
                                 forge::objectdb::indexed_by<forge::objectdb::primary_unique<by_id>,
                                                             forge::objectdb::secondary_unique<by_name, &account::name>>>;

struct byte_less {
   bool operator()(const forge::objectdb::record_key& left, const forge::objectdb::record_key& right) const {
      return left.bytes() < right.bytes();
   }
};

struct memory_state {
   std::map<forge::objectdb::record_key, std::vector<std::byte>, byte_less> records;
   std::size_t flush_calls = 0;
};

class memory_session final : public forge::objectdb::session {
 public:
   memory_session(std::shared_ptr<memory_state> state, bool writes)
       : state_{std::move(state)}, writes_{writes}, working_{state_->records} {}

   [[nodiscard]] forge::objectdb::capabilities capabilities() const noexcept override {
      return forge::objectdb::capabilities{.snapshot_reads = !writes_, .writes = writes_};
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::objectdb::record_key key) override {
      const auto found = working_.find(key);
      if (found == working_.end()) {
         co_return std::nullopt;
      }
      co_return found->second;
   }

   boost::asio::awaitable<void> put(forge::objectdb::record_key key, std::vector<std::byte> value) override {
      if (!writes_) {
         FORGE_THROW_EXCEPTION(forge::objectdb::exceptions::unsupported_operation, "test snapshot is read-only");
      }
      working_[std::move(key)] = std::move(value);
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::objectdb::record_key key) override {
      if (!writes_) {
         FORGE_THROW_EXCEPTION(forge::objectdb::exceptions::unsupported_operation, "test snapshot is read-only");
      }
      working_.erase(key);
      co_return;
   }

   boost::asio::awaitable<forge::objectdb::record_page> scan_page(forge::objectdb::record_range range,
                                                                  forge::objectdb::page_request request) override {
      forge::objectdb::validate_page_request(request);

      auto result = forge::objectdb::record_page{};
      auto current = working_.lower_bound(request.after ? request.after->boundary : range.begin);
      if (request.after && current != working_.end() && current->first == request.after->boundary) {
         ++current;
      }

      auto last_returned = std::optional<forge::objectdb::record_key>{};
      while (current != working_.end()) {
         if (range.has_end && !(current->first.bytes() < range.end.bytes())) {
            break;
         }
         result.entries.push_back(forge::objectdb::record_entry{.key = current->first, .value = current->second});
         last_returned = current->first;
         ++current;
         if (result.entries.size() == request.limit) {
            break;
         }
      }

      if (current != working_.end() && (!range.has_end || current->first.bytes() < range.end.bytes())) {
         result.next = std::move(last_returned);
      }

      co_return result;
   }

   boost::asio::awaitable<void> commit() override {
      if (!writes_) {
         FORGE_THROW_EXCEPTION(forge::objectdb::exceptions::unsupported_operation, "test snapshot cannot commit");
      }
      state_->records = std::move(working_);
      co_return;
   }

   boost::asio::awaitable<void> rollback() override {
      co_return;
   }

 private:
   std::shared_ptr<memory_state> state_;
   bool writes_ = false;
   std::map<forge::objectdb::record_key, std::vector<std::byte>, byte_less> working_;
};

class memory_driver final : public forge::objectdb::driver {
 public:
   boost::asio::awaitable<std::unique_ptr<forge::objectdb::session>> begin_transaction() override {
      co_return std::make_unique<memory_session>(state_, true);
   }

   boost::asio::awaitable<std::unique_ptr<forge::objectdb::session>> begin_read() override {
      co_return std::make_unique<memory_session>(state_, false);
   }

   boost::asio::awaitable<void> flush(bool) override {
      ++state_->flush_calls;
      co_return;
   }

   [[nodiscard]] std::size_t flush_calls() const noexcept {
      return state_->flush_calls;
   }

 private:
   std::shared_ptr<memory_state> state_ = std::make_shared<memory_state>();
};

class installer_plugin final : public forge::app::plugin {
 public:
   explicit installer_plugin(std::shared_ptr<memory_driver> driver) : driver_{std::move(driver)} {}

   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "test.plugins.db.objectdb.installer"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto api = context.apis().get<objectdb_plugin::api>(objectdb_plugin::api::ref());
      co_await api->add_store("accounts", driver_);
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }

 private:
   std::shared_ptr<memory_driver> driver_;
};

[[nodiscard]] forge::app::plugin_descriptor installer_descriptor(std::shared_ptr<memory_driver> driver) {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "test.plugins.db.objectdb.installer"},
      .dependencies = {forge::app::plugin_id{.value = "forge.plugins.db.objectdb"}},
      .factory = [driver = std::move(driver)] { return std::make_unique<installer_plugin>(driver); },
   };
}

[[nodiscard]] std::unique_ptr<forge::app::application_shell>
make_app(forge::config::document document = {}, std::shared_ptr<memory_driver> driver = {}) {
   auto builder = forge::app::application_builder{};
   builder.name("objectdb-plugin-test")
      .runtime(forge::asio::runtime_options{.worker_threads = 1, .thread_name = "objectdb-plugin-test"})
      .plugin(objectdb_plugin::descriptor());
   if (driver) {
      builder.plugin(installer_descriptor(std::move(driver)));
   }

   auto app = std::move(builder).build();
   app->configure(document);
   forge::asio::blocking::run(app->runtime(), app->startup());
   return app;
}

[[nodiscard]] account make_account(std::uint64_t instance, std::string name, std::uint64_t balance) {
   auto value = account{};
   value.id = account::id_type{instance};
   value.name = std::move(name);
   value.balance = balance;
   return value;
}

[[nodiscard]] const forge::config::field_descriptor&
require_field(const forge::config::component_descriptor& descriptor, const std::string& name) {
   const auto found = std::ranges::find_if(descriptor.fields, [&](const auto& field) {
      return field.name == name;
   });
   BOOST_REQUIRE(found != descriptor.fields.end());
   return *found;
}

[[nodiscard]] forge::config::value configured_store(std::string name, std::filesystem::path path) {
   auto object = forge::config::value::object_type{};
   object.emplace("name", forge::config::value{std::move(name)});
   object.emplace("driver", forge::config::value{std::string{"rocksdb"}});
   object.emplace("path", forge::config::value{path.string()});
   object.emplace("family", forge::config::value{std::string{"objectdb"}});
   object.emplace("write-policy", forge::config::value{std::string{"single-writer"}});
   return forge::config::value{std::move(object)};
}

[[nodiscard]] forge::config::document document_for_rocksdb(const std::filesystem::path& path) {
   auto document = forge::config::document{};
   document.set("plugins.db.objectdb.stores", forge::config::value::array_type{configured_store("accounts", path)});
   return document;
}

struct root_guard {
   std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("forge_objectdb_plugin_tests_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

   root_guard() {
      std::filesystem::remove_all(root);
   }

   ~root_guard() {
      std::filesystem::remove_all(root);
   }
};

} // namespace

FORGE_OBJECTDB_OBJECT(account_object)

BOOST_AUTO_TEST_SUITE(objectdb_plugin_test_suite)

BOOST_AUTO_TEST_CASE(objectdb_plugin_descriptor_api_and_config_are_nested) {
   auto plugin = objectdb_plugin::plugin{};
   BOOST_TEST(plugin.id().value == "forge.plugins.db.objectdb");
   BOOST_TEST(objectdb_plugin::api::ref().id.value == "forge.plugins.db.objectdb");

   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "plugins.db.objectdb");

   const auto& stores = require_field(*descriptor, "stores");
   BOOST_TEST(!stores.has_default);

   const auto api_descriptor = objectdb_plugin::api::describe();
   BOOST_TEST(api_descriptor.id.value == "forge.plugins.db.objectdb");
   BOOST_TEST(api_descriptor.methods.empty());
}

BOOST_AUTO_TEST_CASE(objectdb_plugin_rejects_invalid_programmatic_setup) {
   auto runtime = forge::asio::runtime{};
   auto scheduler = forge::asio::task_scheduler{runtime};
   auto apis = forge::api::registry{};
   auto signals = forge::app::signal_bus{};
   auto events = forge::app::event_bus{};
   auto plugin = objectdb_plugin::plugin{};

   auto document = forge::config::document{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::component_view{document, "plugins.db.objectdb"}));
   auto provider = forge::api::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto context = forge::app::plugin_context{scheduler, apis, signals, events};
   forge::asio::blocking::run(runtime, plugin.initialize(context));

   auto api = apis.get<objectdb_plugin::api>(objectdb_plugin::api::ref());
   auto driver = std::make_shared<memory_driver>();

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("", driver)),
                     objectdb_plugin::exceptions::invalid_argument);
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("bad", nullptr)),
                     objectdb_plugin::exceptions::invalid_argument);

   forge::asio::blocking::run(runtime, api->add_store("accounts", driver));
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("accounts", driver)),
                     objectdb_plugin::exceptions::duplicate_store);

   forge::asio::blocking::run(runtime, plugin.startup());
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("late", driver)),
                     objectdb_plugin::exceptions::stopped);

   forge::asio::blocking::run(runtime, plugin.shutdown());
}

BOOST_AUTO_TEST_CASE(objectdb_plugin_rejects_duplicate_configured_store_names) {
   auto runtime = forge::asio::runtime{};
   auto plugin = objectdb_plugin::plugin{};
   auto document = forge::config::document{};
   document.set(
      "plugins.db.objectdb.stores",
      forge::config::value::array_type{
         configured_store("accounts", "/tmp/forge-objectdb-plugin-duplicate-a"),
         configured_store("accounts", "/tmp/forge-objectdb-plugin-duplicate-b"),
      });

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(runtime, plugin.configure(forge::config::component_view{document, "plugins.db.objectdb"})),
      objectdb_plugin::exceptions::invalid_config);
}

BOOST_AUTO_TEST_CASE(objectdb_plugin_custom_driver_store_handle_reads_writes_flushes_and_stops) {
   auto driver = std::make_shared<memory_driver>();
   auto app = make_app({}, driver);
   auto api = app->apis().get<objectdb_plugin::api>(objectdb_plugin::api::ref());

   auto handle = forge::asio::blocking::run(app->runtime(), api->store("accounts"));
   BOOST_TEST(handle.name() == "accounts");
   handle.register_object<account_object>();

   forge::asio::blocking::run(app->runtime(), handle.insert(make_account(42, "alice", 100)));

   const auto loaded = forge::asio::blocking::run(app->runtime(), handle.get(account::id_type{42}));
   BOOST_TEST(loaded.name == "alice");
   BOOST_TEST(loaded.balance == 100U);

   const auto found_by_name =
      forge::asio::blocking::run(app->runtime(), handle.index<account_object, by_name>().find("alice"));
   BOOST_REQUIRE(found_by_name.has_value());
   BOOST_TEST(found_by_name->id.instance == 42U);

   forge::asio::blocking::run(app->runtime(), handle.modify(account::id_type{42}, [](account& value) {
      value.balance += 50;
   }));
   BOOST_TEST(forge::asio::blocking::run(app->runtime(), handle.get(account::id_type{42})).balance == 150U);

   forge::asio::blocking::run(app->runtime(), api->flush("accounts", true));
   BOOST_TEST(driver->flush_calls() == 1U);
   forge::asio::blocking::run(app->runtime(), api->flush_all(true));
   BOOST_TEST(driver->flush_calls() == 2U);

   const auto status = forge::asio::blocking::run(app->runtime(), api->status());
   BOOST_REQUIRE_EQUAL(status.stores.size(), 1U);
   BOOST_TEST(status.stores.front().name == "accounts");
   BOOST_TEST(status.stores.front().driver == "custom");
   BOOST_TEST(status.stores.front().started);

   forge::asio::blocking::run(app->runtime(), app->shutdown());
   BOOST_CHECK_THROW(forge::asio::blocking::run(app->runtime(), handle.find(account::id_type{42})),
                     objectdb_plugin::exceptions::stopped);
}

BOOST_AUTO_TEST_CASE(objectdb_plugin_unknown_store_fails_typed) {
   auto app = make_app();
   auto api = app->apis().get<objectdb_plugin::api>(objectdb_plugin::api::ref());

   BOOST_CHECK_THROW(forge::asio::blocking::run(app->runtime(), api->store("missing")),
                     objectdb_plugin::exceptions::unknown_store);
   BOOST_CHECK_THROW(forge::asio::blocking::run(app->runtime(), api->flush("missing", true)),
                     objectdb_plugin::exceptions::unknown_store);

   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

#if FORGE_HAS_ROCKSDB
BOOST_AUTO_TEST_CASE(objectdb_plugin_configured_rocksdb_store_persists_across_reopen) {
   auto root = root_guard{};
   const auto db_path = root.root / "objectdb";

   {
      auto app = make_app(document_for_rocksdb(db_path));
      auto api = app->apis().get<objectdb_plugin::api>(objectdb_plugin::api::ref());
      auto handle = forge::asio::blocking::run(app->runtime(), api->store("accounts"));
      handle.register_object<account_object>();

      forge::asio::blocking::run(app->runtime(), handle.insert(make_account(7, "persisted", 900)));
      forge::asio::blocking::run(app->runtime(), api->flush_all(true));
      forge::asio::blocking::run(app->runtime(), app->shutdown());
   }

   {
      auto app = make_app(document_for_rocksdb(db_path));
      auto api = app->apis().get<objectdb_plugin::api>(objectdb_plugin::api::ref());
      auto handle = forge::asio::blocking::run(app->runtime(), api->store("accounts"));
      handle.register_object<account_object>();

      const auto loaded = forge::asio::blocking::run(app->runtime(), handle.get(account::id_type{7}));
      BOOST_TEST(loaded.name == "persisted");
      BOOST_TEST(loaded.balance == 900U);

      auto snapshot = forge::asio::blocking::run(app->runtime(), handle.begin_read());
      const auto from_snapshot = forge::asio::blocking::run(app->runtime(), snapshot.get(account::id_type{7}));
      BOOST_TEST(from_snapshot.name == "persisted");

      forge::asio::blocking::run(app->runtime(), app->shutdown());
   }
}
#endif

BOOST_AUTO_TEST_SUITE_END()
