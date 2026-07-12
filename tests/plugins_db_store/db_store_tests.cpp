#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/exceptions/macros.hpp>
#include <forge/db/object/macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

import forge.api.core.binding;
import forge.api.core.registry;
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
import forge.config.core.component;
import forge.config.core.document;
import forge.config.core.value;
import forge.ids.object_id;
import forge.db.blob.ref;
import forge.db.blob.store;
import forge.db.blob.transaction;
import forge.db.blob.types;
import forge.db.object.cursor;
import forge.db.object.exceptions;
import forge.db.object.header;
import forge.db.object.index;
import forge.db.object.object;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.object.store;
import forge.plugins.db.store.api;
import forge.plugins.db.store.exceptions;
import forge.plugins.db.store.plugin;
import forge.plugins.db.store.types;
import forge.raw.raw;

#if FORGE_HAS_ROCKSDB
import forge.db.rocksdb.driver;
#endif

namespace {

namespace store_plugin = forge::plugins::db::store;

struct by_id;
struct by_name;
struct by_path;

struct account : forge::db::object::object<account, 1, 7> {
   std::string name;
   std::uint64_t balance = 0;

   bool operator==(const account&) const = default;
};

BOOST_DESCRIBE_STRUCT(account, (forge::db::object::object<account, 1, 7>), (name, balance))

using account_object =
   forge::db::object::object_index<account,
                                 forge::db::object::indexed_by<forge::db::object::primary_unique<by_id>,
                                                             forge::db::object::ordered_unique<
                                                                by_name, forge::db::object::member<&account::name>>>>;

struct file_record : forge::db::object::object<file_record, 1, 8> {
   std::string path;
   forge::db::blob::ref<> content;

   bool operator==(const file_record&) const = default;
};

BOOST_DESCRIBE_STRUCT(file_record, (forge::db::object::object<file_record, 1, 8>), (path, content))

using file_object =
   forge::db::object::object_index<file_record,
                                   forge::db::object::indexed_by<
                                      forge::db::object::primary_unique<by_id>,
                                      forge::db::object::ordered_unique<
                                         by_path, forge::db::object::member<&file_record::path>>>>;

struct byte_less {
   bool operator()(const forge::db::core::record_key& left, const forge::db::core::record_key& right) const {
      return left.bytes() < right.bytes();
   }
};

struct memory_state {
   std::map<std::string, std::map<forge::db::core::record_key, std::vector<std::byte>, byte_less>> records;
   std::size_t flush_calls = 0;
   std::size_t active_writes = 0;
   bool overlapping_writes = false;
};

class memory_session final : public forge::db::core::session {
 public:
   memory_session(std::shared_ptr<memory_state> state, bool writes) : state_{std::move(state)}, writes_{writes}, working_{state_->records} {
      if (writes_) {
         ++state_->active_writes;
         if (state_->active_writes > 1U) {
            state_->overlapping_writes = true;
         }
      }
   }

   ~memory_session() override {
      close();
   }

   [[nodiscard]] forge::db::core::capabilities capabilities() const noexcept override {
      return forge::db::core::capabilities{.snapshot_reads = !writes_, .writes = writes_};
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::db::core::family family, forge::db::core::record_key key) override {
      const auto family_found = working_.find(family.name);
      if (family_found == working_.end()) {
         co_return std::nullopt;
      }
      const auto found = family_found->second.find(key);
      if (found == family_found->second.end()) {
         co_return std::nullopt;
      }
      co_return found->second;
   }

   boost::asio::awaitable<void> put(forge::db::core::family family, forge::db::core::record_key key, std::vector<std::byte> value) override {
      if (!writes_) {
         FORGE_THROW_EXCEPTION(forge::db::object::exceptions::unsupported_operation, "test snapshot is read-only");
      }
      working_[family.name][std::move(key)] = std::move(value);
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::db::core::family family, forge::db::core::record_key key) override {
      if (!writes_) {
         FORGE_THROW_EXCEPTION(forge::db::object::exceptions::unsupported_operation, "test snapshot is read-only");
      }
      working_[family.name].erase(key);
      co_return;
   }

   boost::asio::awaitable<forge::db::core::record_page> scan_page(forge::db::core::family family, forge::db::core::record_range range,
                                                                  forge::db::core::page_request request) override {
      forge::db::object::validate_page_request(request);

      auto result = forge::db::core::record_page{};
      auto& records = working_[family.name];
      auto current = records.lower_bound(request.after ? request.after->boundary : range.begin);
      if (request.after && current != records.end() && current->first == request.after->boundary) {
         ++current;
      }

      auto last_returned = std::optional<forge::db::core::record_key>{};
      while (current != records.end()) {
         if (range.has_end && !(current->first.bytes() < range.end.bytes())) {
            break;
         }
         result.entries.push_back(forge::db::core::record_entry{.key = current->first, .value = current->second});
         last_returned = current->first;
         ++current;
         if (result.entries.size() == request.limit) {
            break;
         }
      }

      if (current != records.end() && (!range.has_end || current->first.bytes() < range.end.bytes())) {
         result.next = forge::db::core::cursor{.boundary = std::move(*last_returned)};
      }

      co_return result;
   }

   boost::asio::awaitable<void> commit() override {
      if (!writes_) {
         FORGE_THROW_EXCEPTION(forge::db::object::exceptions::unsupported_operation, "test snapshot cannot commit");
      }
      state_->records = std::move(working_);
      close();
      co_return;
   }

   boost::asio::awaitable<void> rollback() override {
      close();
      co_return;
   }

 private:
   void close() noexcept {
      if (writes_ && !closed_) {
         closed_ = true;
         --state_->active_writes;
      }
   }

   std::shared_ptr<memory_state> state_;
   bool writes_ = false;
   bool closed_ = false;
   std::map<std::string, std::map<forge::db::core::record_key, std::vector<std::byte>, byte_less>> working_;
};

class memory_driver final : public forge::db::core::driver {
 public:
   boost::asio::awaitable<void> async_flush(bool) override {
      ++state_->flush_calls;
      co_return;
   }

   [[nodiscard]] std::size_t flush_calls() const noexcept {
      return state_->flush_calls;
   }

   [[nodiscard]] std::size_t active_writes() const noexcept {
      return state_->active_writes;
   }

   [[nodiscard]] bool overlapping_writes() const noexcept {
      return state_->overlapping_writes;
   }

   void seed_record(forge::db::core::family family,
                    forge::db::core::record_key key,
                    std::vector<std::byte> value) {
      state_->records[family.name][std::move(key)] = std::move(value);
   }

 private:
   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>> open_transaction() override {
      co_return std::make_unique<memory_session>(state_, true);
   }

   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>> open_snapshot() override {
      co_return std::make_unique<memory_session>(state_, false);
   }

   std::shared_ptr<memory_state> state_ = std::make_shared<memory_state>();
};

class installer_plugin final : public forge::app::plugin {
 public:
   explicit installer_plugin(std::shared_ptr<memory_driver> driver) : driver_{std::move(driver)} {}

   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "test.plugins.db.store.installer"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto api = context.apis().get<store_plugin::api>(store_plugin::api::ref());
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
      .id = forge::app::plugin_id{.value = "test.plugins.db.store.installer"},
      .dependencies = {forge::app::plugin_id{.value = "forge.plugins.db.store"}},
      .factory = [driver = std::move(driver)] { return std::make_unique<installer_plugin>(driver); },
   };
}

[[nodiscard]] std::unique_ptr<forge::app::application_shell>
make_app(forge::config::core::document document = {}, std::shared_ptr<memory_driver> driver = {}) {
   auto builder = forge::app::application_builder{};
   builder.name("db-store-plugin-test")
      .runtime(forge::asio::runtime_options{.worker_threads = 1, .thread_name = "db-store-plugin-test"})
      .plugin(store_plugin::descriptor());
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
   value.id = decltype(value.id){instance};
   value.name = std::move(name);
   value.balance = balance;
   return value;
}

[[nodiscard]] file_record make_file(std::uint64_t instance,
                                    std::string path,
                                    forge::db::blob::ref<> content) {
   auto value = file_record{};
   value.id = decltype(value.id){instance};
   value.path = std::move(path);
   value.content = std::move(content);
   return value;
}

[[nodiscard]] std::vector<std::byte> bytes(std::string_view value) {
   return std::vector<std::byte>{
      reinterpret_cast<const std::byte*>(value.data()),
      reinterpret_cast<const std::byte*>(value.data() + value.size()),
   };
}

[[nodiscard]] forge::db::core::record_key header_record_key() {
   return forge::db::core::record_key{std::vector<std::byte>(11U, std::byte{0U})};
}

[[nodiscard]] std::vector<std::byte> packed_header(std::uint32_t version) {
   auto value = forge::db::object::header{};
   value.id = forge::db::object::header_id;
   value.version = version;
   const auto packed = forge::raw::pack(value);
   auto result = std::vector<std::byte>{};
   result.reserve(packed.size());
   for (const auto byte : packed) {
      result.push_back(static_cast<std::byte>(byte));
   }
   return result;
}

[[nodiscard]] const forge::config::core::field_descriptor&
require_field(const forge::config::core::component_descriptor& descriptor, const std::string& name) {
   const auto found = std::ranges::find_if(descriptor.fields, [&](const auto& field) {
      return field.name == name;
   });
   BOOST_REQUIRE(found != descriptor.fields.end());
   return *found;
}

[[nodiscard]] forge::config::core::value configured_store(std::string name, std::filesystem::path path) {
   auto object = forge::config::core::value::object_type{};
   object.emplace("name", forge::config::core::value{std::move(name)});
   object.emplace("driver", forge::config::core::value{std::string{"rocksdb"}});
   object.emplace("path", forge::config::core::value{path.string()});
   auto object_layer = forge::config::core::value::object_type{};
   object_layer.emplace("family", forge::config::core::value{std::string{"objectdb"}});
   object_layer.emplace("write-policy", forge::config::core::value{std::string{"single-writer"}});
   object.emplace("object", forge::config::core::value{std::move(object_layer)});
   return forge::config::core::value{std::move(object)};
}

[[nodiscard]] forge::config::core::value configured_object_blob_store(std::string name, std::filesystem::path path) {
   auto object = forge::config::core::value::object_type{};
   object.emplace("name", forge::config::core::value{std::move(name)});
   object.emplace("driver", forge::config::core::value{std::string{"rocksdb"}});
   object.emplace("path", forge::config::core::value{path.string()});

   auto object_layer = forge::config::core::value::object_type{};
   object_layer.emplace("family", forge::config::core::value{std::string{"objectdb"}});
   object_layer.emplace("write-policy", forge::config::core::value{std::string{"single-writer"}});
   object.emplace("object", forge::config::core::value{std::move(object_layer)});

   auto data_blobs = forge::config::core::value::object_type{};
   data_blobs.emplace("enable-blob-files", forge::config::core::value{true});
   data_blobs.emplace("min-blob-size", forge::config::core::value{std::uint64_t{16U}});

   auto blob_layer = forge::config::core::value::object_type{};
   blob_layer.emplace("data-family", forge::config::core::value{std::string{"blobdb.data"}});
   blob_layer.emplace("refs-family", forge::config::core::value{std::string{"blobdb.refs"}});
   blob_layer.emplace("data-blobs", forge::config::core::value{std::move(data_blobs)});
   object.emplace("blob", forge::config::core::value{std::move(blob_layer)});

   return forge::config::core::value{std::move(object)};
}

[[nodiscard]] forge::config::core::document document_for_rocksdb(const std::filesystem::path& path) {
   auto document = forge::config::core::document{};
   document.set("plugins.db.store.stores", forge::config::core::value::array_type{configured_store("accounts", path)});
   return document;
}

struct root_guard {
   std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("forge_db_store_plugin_tests_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

   root_guard() {
      std::filesystem::remove_all(root);
   }

   ~root_guard() {
      std::filesystem::remove_all(root);
   }
};

} // namespace

FORGE_DB_OBJECT(account_object)
FORGE_DB_OBJECT(file_object)

template <typename Handle>
concept can_register_system_header = requires(const Handle& handle) {
   handle.template register_object<forge::db::object::header_index>();
};

template <typename Handle>
concept can_insert_system_header = requires(const Handle& handle, forge::db::object::header value) {
   handle.insert(value);
};

static_assert(!can_register_system_header<store_plugin::object_handle>);
static_assert(!can_insert_system_header<store_plugin::object_handle>);

BOOST_AUTO_TEST_SUITE(store_plugin_test_suite)

BOOST_AUTO_TEST_CASE(store_plugin_descriptor_api_and_config_are_nested) {
   auto plugin = store_plugin::plugin{};
   BOOST_TEST(plugin.id().value == "forge.plugins.db.store");
   BOOST_TEST(store_plugin::api::ref().id.value == "forge.plugins.db.store");

   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "plugins.db.store");

   const auto& stores = require_field(*descriptor, "stores");
   BOOST_TEST(!stores.has_default);

   const auto api_descriptor = store_plugin::api::describe();
   BOOST_TEST(api_descriptor.id.value == "forge.plugins.db.store");
   BOOST_TEST(api_descriptor.methods.empty());
}

BOOST_AUTO_TEST_CASE(store_plugin_rejects_invalid_programmatic_setup) {
   auto runtime = forge::asio::runtime{};
   auto scheduler = forge::asio::task_scheduler{runtime};
   auto apis = forge::api::core::registry{};
   auto signals = forge::app::signal_bus{};
   auto events = forge::app::event_bus{};
   auto plugin = store_plugin::plugin{};

   auto document = forge::config::core::document{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"}));
   auto provider = forge::api::core::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto context = forge::app::plugin_context{scheduler, apis, signals, events};
   forge::asio::blocking::run(runtime, plugin.initialize(context));

   auto api = apis.get<store_plugin::api>(store_plugin::api::ref());
   auto driver = std::make_shared<memory_driver>();

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("", driver)),
                     store_plugin::exceptions::invalid_argument);
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("bad", nullptr)),
                     store_plugin::exceptions::invalid_argument);

   forge::asio::blocking::run(runtime, api->add_store("accounts", driver));
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("accounts", driver)),
                     store_plugin::exceptions::duplicate_store);

   forge::asio::blocking::run(runtime, plugin.startup());
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("late", driver)),
                     store_plugin::exceptions::stopped);

   forge::asio::blocking::run(runtime, plugin.shutdown());
}

BOOST_AUTO_TEST_CASE(store_plugin_does_not_publish_object_store_with_incompatible_header) {
   auto runtime = forge::asio::runtime{};
   auto scheduler = forge::asio::task_scheduler{runtime};
   auto apis = forge::api::core::registry{};
   auto signals = forge::app::signal_bus{};
   auto events = forge::app::event_bus{};
   auto plugin = store_plugin::plugin{};
   auto driver = std::make_shared<memory_driver>();
   driver->seed_record(
      forge::db::core::family{"objectdb"},
      header_record_key(),
      packed_header(forge::db::object::header::current_version + 1U));

   auto document = forge::config::core::document{};
   forge::asio::blocking::run(
      runtime,
      plugin.configure(forge::config::core::component_view{document, "plugins.db.store"}));
   auto provider = forge::api::core::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto context = forge::app::plugin_context{scheduler, apis, signals, events};
   forge::asio::blocking::run(runtime, plugin.initialize(context));

   auto api = apis.get<store_plugin::api>(store_plugin::api::ref());
   forge::asio::blocking::run(runtime, api->add_store("accounts", driver));

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, plugin.startup()),
                     store_plugin::exceptions::startup_failed);

   const auto status = forge::asio::blocking::run(runtime, api->status());
   BOOST_REQUIRE_EQUAL(status.stores.size(), 1U);
   BOOST_CHECK(!status.stores.front().started);

   const auto handle = forge::asio::blocking::run(runtime, api->store("accounts"));
   BOOST_CHECK_THROW((void)handle.objects(), store_plugin::exceptions::stopped);
   BOOST_CHECK_EQUAL(driver->active_writes(), 0U);

   forge::asio::blocking::run(runtime, plugin.shutdown());
}

BOOST_AUTO_TEST_CASE(store_plugin_rejects_programmatic_overlapping_layer_families) {
   auto runtime = forge::asio::runtime{};
   auto scheduler = forge::asio::task_scheduler{runtime};
   auto apis = forge::api::core::registry{};
   auto signals = forge::app::signal_bus{};
   auto events = forge::app::event_bus{};
   auto plugin = store_plugin::plugin{};

   auto document = forge::config::core::document{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"}));
   auto provider = forge::api::core::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto context = forge::app::plugin_context{scheduler, apis, signals, events};
   forge::asio::blocking::run(runtime, plugin.initialize(context));

   auto api = apis.get<store_plugin::api>(store_plugin::api::ref());
   auto driver = std::make_shared<memory_driver>();

   auto object_data_overlap = store_plugin::store_options{};
   object_data_overlap.object = store_plugin::object_layer_options{.family = forge::db::core::family{"shared"}};
   object_data_overlap.blob = store_plugin::blob_layer_options{
      .data_family = forge::db::core::family{"shared"},
      .refs_family = forge::db::core::family{"blob.refs"},
   };
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("object-data", driver, object_data_overlap)),
                     store_plugin::exceptions::invalid_argument);

   auto object_refs_overlap = store_plugin::store_options{};
   object_refs_overlap.object = store_plugin::object_layer_options{.family = forge::db::core::family{"shared"}};
   object_refs_overlap.blob = store_plugin::blob_layer_options{
      .data_family = forge::db::core::family{"blob.data"},
      .refs_family = forge::db::core::family{"shared"},
   };
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("object-refs", driver, object_refs_overlap)),
                     store_plugin::exceptions::invalid_argument);

   auto blob_overlap = store_plugin::store_options{};
   blob_overlap.blob = store_plugin::blob_layer_options{
      .data_family = forge::db::core::family{"blob.shared"},
      .refs_family = forge::db::core::family{"blob.shared"},
   };
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, api->add_store("blob-overlap", driver, blob_overlap)),
                     store_plugin::exceptions::invalid_argument);
}

BOOST_AUTO_TEST_CASE(store_plugin_rejects_duplicate_configured_store_names) {
   auto runtime = forge::asio::runtime{};
   auto plugin = store_plugin::plugin{};
   auto document = forge::config::core::document{};
   document.set(
      "plugins.db.store.stores",
      forge::config::core::value::array_type{
         configured_store("accounts", "/tmp/forge-db-store-plugin-duplicate-a"),
         configured_store("accounts", "/tmp/forge-db-store-plugin-duplicate-b"),
      });

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"})),
      store_plugin::exceptions::invalid_config);
}

BOOST_AUTO_TEST_CASE(store_plugin_rejects_configured_store_without_layers) {
   auto runtime = forge::asio::runtime{};
   auto plugin = store_plugin::plugin{};
   auto store = forge::config::core::value::object_type{};
   store.emplace("name", forge::config::core::value{std::string{"empty"}});
   store.emplace("driver", forge::config::core::value{std::string{"rocksdb"}});
   store.emplace("path", forge::config::core::value{std::string{"/tmp/forge-db-store-plugin-no-layers"}});

   auto document = forge::config::core::document{};
   document.set("plugins.db.store.stores", forge::config::core::value::array_type{forge::config::core::value{std::move(store)}});

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"})),
      store_plugin::exceptions::invalid_config);
}

BOOST_AUTO_TEST_CASE(store_plugin_rejects_configured_overlapping_layer_families) {
   auto runtime = forge::asio::runtime{};

   auto expect_invalid = [&](std::string object_family, std::string data_family, std::string refs_family) {
      auto plugin = store_plugin::plugin{};
      auto store = forge::config::core::value::object_type{};
      store.emplace("name", forge::config::core::value{std::string{"files"}});
      store.emplace("driver", forge::config::core::value{std::string{"rocksdb"}});
      store.emplace("path", forge::config::core::value{std::string{"/tmp/forge-db-store-plugin-overlap"}});

      auto object_layer = forge::config::core::value::object_type{};
      object_layer.emplace("family", forge::config::core::value{std::move(object_family)});
      store.emplace("object", forge::config::core::value{std::move(object_layer)});

      auto blob_layer = forge::config::core::value::object_type{};
      blob_layer.emplace("data-family", forge::config::core::value{std::move(data_family)});
      blob_layer.emplace("refs-family", forge::config::core::value{std::move(refs_family)});
      store.emplace("blob", forge::config::core::value{std::move(blob_layer)});

      auto document = forge::config::core::document{};
      document.set("plugins.db.store.stores", forge::config::core::value::array_type{forge::config::core::value{std::move(store)}});

      BOOST_CHECK_THROW(
         forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"})),
         store_plugin::exceptions::invalid_config);
   };

   expect_invalid("shared", "shared", "blob.refs");
   expect_invalid("shared", "blob.data", "shared");
   expect_invalid("objectdb", "blob.shared", "blob.shared");
}

BOOST_AUTO_TEST_CASE(store_plugin_rejects_configure_after_stop_or_shutdown) {
   auto runtime = forge::asio::runtime{};
   auto document = forge::config::core::document{};

   {
      auto plugin = store_plugin::plugin{};
      plugin.request_stop();

      BOOST_CHECK_THROW(
         forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"})),
         store_plugin::exceptions::stopped);
      BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, plugin.startup()),
                        store_plugin::exceptions::startup_failed);
   }

   {
      auto plugin = store_plugin::plugin{};
      forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"}));
      forge::asio::blocking::run(runtime, plugin.startup());
      forge::asio::blocking::run(runtime, plugin.shutdown());

      BOOST_CHECK_THROW(
         forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"})),
         store_plugin::exceptions::stopped);
   }
}

BOOST_AUTO_TEST_CASE(store_plugin_custom_driver_store_handle_reads_writes_flushes_and_stops) {
   auto driver = std::make_shared<memory_driver>();
   auto app = make_app({}, driver);
   auto api = app->apis().get<store_plugin::api>(store_plugin::api::ref());

   auto handle = forge::asio::blocking::run(app->runtime(), api->store("accounts"));
   BOOST_TEST(handle.name() == "accounts");
   BOOST_CHECK_THROW((void)handle.blobs(), store_plugin::exceptions::unavailable_layer);
   handle.objects().register_object<account_object>();

   forge::asio::blocking::run(app->runtime(), handle.objects().insert(make_account(42, "alice", 100)));

   const auto loaded = forge::asio::blocking::run(app->runtime(), handle.objects().get(decltype(account{}.id){42}));
   BOOST_TEST(loaded.name == "alice");
   BOOST_TEST(loaded.balance == 100U);

   const auto found_by_name =
      forge::asio::blocking::run(app->runtime(), handle.objects().index<account_object, by_name>().find("alice"));
   BOOST_REQUIRE(found_by_name.has_value());
   BOOST_TEST(found_by_name->id.instance == 42U);

   forge::asio::blocking::run(app->runtime(), handle.objects().modify(decltype(account{}.id){42}, [](account& value) {
      value.balance += 50;
   }));
   BOOST_TEST(forge::asio::blocking::run(app->runtime(), handle.objects().get(decltype(account{}.id){42})).balance == 150U);

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
   BOOST_CHECK_THROW(forge::asio::blocking::run(app->runtime(), handle.objects().find(decltype(account{}.id){42})),
                     store_plugin::exceptions::stopped);
}

BOOST_AUTO_TEST_CASE(store_plugin_blob_only_programmatic_store_rejects_objects_and_roundtrips_blob) {
   auto runtime = forge::asio::runtime{};
   auto scheduler = forge::asio::task_scheduler{runtime};
   auto apis = forge::api::core::registry{};
   auto signals = forge::app::signal_bus{};
   auto events = forge::app::event_bus{};
   auto plugin = store_plugin::plugin{};
   auto driver = std::make_shared<memory_driver>();

   auto document = forge::config::core::document{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"}));
   auto provider = forge::api::core::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto context = forge::app::plugin_context{scheduler, apis, signals, events};
   forge::asio::blocking::run(runtime, plugin.initialize(context));

   auto options = store_plugin::store_options{};
   options.object.reset();
   options.blob = store_plugin::blob_layer_options{};

   auto api = apis.get<store_plugin::api>(store_plugin::api::ref());
   forge::asio::blocking::run(runtime, api->add_store("blobs", driver, options));
   forge::asio::blocking::run(runtime, plugin.startup());

   auto handle = forge::asio::blocking::run(runtime, api->store("blobs"));
   BOOST_CHECK_THROW((void)handle.objects(), store_plugin::exceptions::unavailable_layer);

   auto content = forge::asio::blocking::run(runtime, handle.blobs().put(bytes("blob-only-payload")));
   BOOST_TEST(content.size == 17U);
   BOOST_TEST(forge::asio::blocking::run(runtime, handle.blobs().has(content)));
   BOOST_TEST(forge::asio::blocking::run(runtime, handle.blobs().get(content)).size() == 17U);

   forge::asio::blocking::run(runtime, plugin.shutdown());
}

BOOST_AUTO_TEST_CASE(store_plugin_shared_transaction_commits_object_metadata_and_blob_payload) {
   auto runtime = forge::asio::runtime{};
   auto scheduler = forge::asio::task_scheduler{runtime};
   auto apis = forge::api::core::registry{};
   auto signals = forge::app::signal_bus{};
   auto events = forge::app::event_bus{};
   auto plugin = store_plugin::plugin{};
   auto driver = std::make_shared<memory_driver>();

   auto document = forge::config::core::document{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"}));
   auto provider = forge::api::core::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto context = forge::app::plugin_context{scheduler, apis, signals, events};
   forge::asio::blocking::run(runtime, plugin.initialize(context));

   auto options = store_plugin::store_options{};
   options.blob = store_plugin::blob_layer_options{};

   auto api = apis.get<store_plugin::api>(store_plugin::api::ref());
   forge::asio::blocking::run(runtime, api->add_store("files", driver, options));
   forge::asio::blocking::run(runtime, plugin.startup());

   auto handle = forge::asio::blocking::run(runtime, api->store("files"));
   handle.objects().register_object<file_object>();

   auto tx = forge::asio::blocking::run(runtime, handle.begin_transaction());
   auto object_tx = handle.objects().join(tx);
   auto blob_tx = handle.blobs().join(tx);

   auto content = forge::asio::blocking::run(runtime, blob_tx.put(bytes("shared payload")));
   forge::asio::blocking::run(runtime, blob_tx.retain(content, forge::db::blob::owner_ref{"file:1"}));
   forge::asio::blocking::run(runtime, object_tx.insert(make_file(1, "/a.txt", content)));
   forge::asio::blocking::run(runtime, tx.commit());

   const auto loaded = forge::asio::blocking::run(runtime, handle.objects().get(decltype(file_record{}.id){1}));
   BOOST_TEST(loaded.path == "/a.txt");
   BOOST_TEST(loaded.content == content);
   BOOST_TEST(forge::asio::blocking::run(runtime, handle.blobs().get(loaded.content)).size() == 14U);
   BOOST_TEST(forge::asio::blocking::run(runtime, handle.blobs().ref_count(content)) == 1U);

   forge::asio::blocking::run(runtime, plugin.shutdown());
}

BOOST_AUTO_TEST_CASE(store_plugin_begin_transaction_preserves_object_single_writer_gate) {
   auto driver = std::make_shared<memory_driver>();
   auto app = make_app({}, driver);
   auto api = app->apis().get<store_plugin::api>(store_plugin::api::ref());
   auto handle = forge::asio::blocking::run(app->runtime(), api->store("accounts"));
   handle.objects().register_object<account_object>();

   forge::asio::blocking::run(app->runtime(), [&]() -> boost::asio::awaitable<void> {
      auto first = co_await handle.begin_transaction();

      auto second_started = std::make_shared<bool>(false);
      auto second_error = std::make_shared<std::exception_ptr>();
      const auto executor = co_await boost::asio::this_coro::executor;
      boost::asio::co_spawn(
         executor,
         [handle, second_started, second_error]() mutable -> boost::asio::awaitable<void> {
            try {
               auto second = co_await handle.begin_transaction();
               *second_started = true;
               co_await second.rollback();
            } catch (...) {
               *second_error = std::current_exception();
            }
            co_return;
         },
         boost::asio::detached);

      auto timer = boost::asio::steady_timer{executor};
      timer.expires_after(std::chrono::milliseconds{50});
      co_await timer.async_wait(boost::asio::use_awaitable);

      BOOST_CHECK(!*second_started);
      BOOST_CHECK(!driver->overlapping_writes());

      co_await first.rollback();

      timer.expires_after(std::chrono::milliseconds{50});
      co_await timer.async_wait(boost::asio::use_awaitable);

      if (*second_error) {
         std::rethrow_exception(*second_error);
      }
      BOOST_CHECK(*second_started);
      BOOST_CHECK(!driver->overlapping_writes());
      BOOST_CHECK_EQUAL(driver->active_writes(), 0U);
      co_return;
   }());

   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

BOOST_AUTO_TEST_CASE(store_plugin_shared_transaction_rollback_hides_object_and_blob) {
   auto runtime = forge::asio::runtime{};
   auto scheduler = forge::asio::task_scheduler{runtime};
   auto apis = forge::api::core::registry{};
   auto signals = forge::app::signal_bus{};
   auto events = forge::app::event_bus{};
   auto plugin = store_plugin::plugin{};
   auto driver = std::make_shared<memory_driver>();

   auto document = forge::config::core::document{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"}));
   auto provider = forge::api::core::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto context = forge::app::plugin_context{scheduler, apis, signals, events};
   forge::asio::blocking::run(runtime, plugin.initialize(context));

   auto options = store_plugin::store_options{};
   options.blob = store_plugin::blob_layer_options{};

   auto api = apis.get<store_plugin::api>(store_plugin::api::ref());
   forge::asio::blocking::run(runtime, api->add_store("files", driver, options));
   forge::asio::blocking::run(runtime, plugin.startup());

   auto handle = forge::asio::blocking::run(runtime, api->store("files"));
   handle.objects().register_object<file_object>();

   auto tx = forge::asio::blocking::run(runtime, handle.begin_transaction());
   auto object_tx = handle.objects().join(tx);
   auto blob_tx = handle.blobs().join(tx);

   auto content = forge::asio::blocking::run(runtime, blob_tx.put(bytes("rollback payload")));
   forge::asio::blocking::run(runtime, object_tx.insert(make_file(2, "/rollback.txt", content)));
   forge::asio::blocking::run(runtime, tx.rollback());

   BOOST_TEST(!forge::asio::blocking::run(runtime, handle.objects().find(decltype(file_record{}.id){2})).has_value());
   BOOST_TEST(!forge::asio::blocking::run(runtime, handle.blobs().has(content)));

   forge::asio::blocking::run(runtime, plugin.shutdown());
}

BOOST_AUTO_TEST_CASE(store_plugin_shared_transaction_object_failure_rolls_back_blob_payload) {
   auto runtime = forge::asio::runtime{};
   auto scheduler = forge::asio::task_scheduler{runtime};
   auto apis = forge::api::core::registry{};
   auto signals = forge::app::signal_bus{};
   auto events = forge::app::event_bus{};
   auto plugin = store_plugin::plugin{};
   auto driver = std::make_shared<memory_driver>();

   auto document = forge::config::core::document{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"}));
   auto provider = forge::api::core::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto context = forge::app::plugin_context{scheduler, apis, signals, events};
   forge::asio::blocking::run(runtime, plugin.initialize(context));

   auto options = store_plugin::store_options{};
   options.blob = store_plugin::blob_layer_options{};

   auto api = apis.get<store_plugin::api>(store_plugin::api::ref());
   forge::asio::blocking::run(runtime, api->add_store("files", driver, options));
   forge::asio::blocking::run(runtime, plugin.startup());

   auto handle = forge::asio::blocking::run(runtime, api->store("files"));
   handle.objects().register_object<file_object>();
   forge::asio::blocking::run(runtime, handle.objects().insert(make_file(1, "/duplicate.txt", {})));

   auto tx = forge::asio::blocking::run(runtime, handle.begin_transaction());
   auto object_tx = handle.objects().join(tx);
   auto blob_tx = handle.blobs().join(tx);

   auto content = forge::asio::blocking::run(runtime, blob_tx.put(bytes("orphan candidate")));
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, object_tx.insert(make_file(2, "/duplicate.txt", content))),
                     forge::db::object::exceptions::duplicate_object);
   forge::asio::blocking::run(runtime, tx.rollback());

   BOOST_TEST(!forge::asio::blocking::run(runtime, handle.blobs().has(content)));
   const auto existing = forge::asio::blocking::run(runtime, handle.objects().get(decltype(file_record{}.id){1}));
   BOOST_TEST(existing.path == "/duplicate.txt");
   BOOST_TEST(!driver->overlapping_writes());

   forge::asio::blocking::run(runtime, plugin.shutdown());
}

BOOST_AUTO_TEST_CASE(store_plugin_store_handle_remains_valid_during_dependent_shutdown) {
   auto runtime = forge::asio::runtime{};
   auto scheduler = forge::asio::task_scheduler{runtime};
   auto apis = forge::api::core::registry{};
   auto signals = forge::app::signal_bus{};
   auto events = forge::app::event_bus{};
   auto plugin = store_plugin::plugin{};
   auto driver = std::make_shared<memory_driver>();

   auto document = forge::config::core::document{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"}));
   auto provider = forge::api::core::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto context = forge::app::plugin_context{scheduler, apis, signals, events};
   forge::asio::blocking::run(runtime, plugin.initialize(context));

   auto api = apis.get<store_plugin::api>(store_plugin::api::ref());
   forge::asio::blocking::run(runtime, api->add_store("shutdown", driver));
   forge::asio::blocking::run(runtime, plugin.startup());

   auto handle = forge::asio::blocking::run(runtime, api->store("shutdown"));
   handle.objects().register_object<account_object>();
   forge::asio::blocking::run(runtime, handle.objects().insert(make_account(9, "startup", 1)));

   plugin.request_stop();

   forge::asio::blocking::run(runtime, handle.objects().replace(make_account(9, "shutdown", 77)));
   forge::asio::blocking::run(runtime, api->flush("shutdown", true));
   const auto loaded = forge::asio::blocking::run(
      runtime, handle.objects().get<account_object>(forge::ids::object_id{.space = 1, .type = 7, .instance = 9}));
   BOOST_TEST(loaded.balance == 77U);
   BOOST_TEST(driver->flush_calls() == 1U);

   forge::asio::blocking::run(runtime, plugin.shutdown());
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, handle.objects().find(decltype(account{}.id){9})),
                     store_plugin::exceptions::stopped);
}

BOOST_AUTO_TEST_CASE(store_plugin_store_handle_concurrent_close_is_snapshot_safe) {
   auto runtime = forge::asio::runtime{};
   auto scheduler = forge::asio::task_scheduler{runtime};
   auto apis = forge::api::core::registry{};
   auto signals = forge::app::signal_bus{};
   auto events = forge::app::event_bus{};
   auto plugin = store_plugin::plugin{};
   auto driver = std::make_shared<memory_driver>();

   auto document = forge::config::core::document{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::core::component_view{document, "plugins.db.store"}));
   auto provider = forge::api::core::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto context = forge::app::plugin_context{scheduler, apis, signals, events};
   forge::asio::blocking::run(runtime, plugin.initialize(context));

   auto api = apis.get<store_plugin::api>(store_plugin::api::ref());
   forge::asio::blocking::run(runtime, api->add_store("shutdown", driver));
   forge::asio::blocking::run(runtime, plugin.startup());

   auto handle = forge::asio::blocking::run(runtime, api->store("shutdown"));
   handle.objects().register_object<account_object>();
   forge::asio::blocking::run(runtime, handle.objects().insert(make_account(10, "close-race", 1)));

   plugin.request_stop();

   auto done = std::atomic_bool{false};
   auto closer_error = std::exception_ptr{};
   auto closer = std::thread{[&] {
      try {
         forge::asio::blocking::run(runtime, plugin.shutdown());
      } catch (...) {
         closer_error = std::current_exception();
      }
      done.store(true, std::memory_order_release);
   }};

   auto successes = std::size_t{0};
   auto stopped = std::size_t{0};
   do {
      try {
         (void)forge::asio::blocking::run(runtime, handle.objects().find(decltype(account{}.id){10}));
         ++successes;
      } catch (const store_plugin::exceptions::stopped&) {
         ++stopped;
      }
   } while (!done.load(std::memory_order_acquire));

   closer.join();
   if (closer_error) {
      std::rethrow_exception(closer_error);
   }

   BOOST_TEST(successes + stopped > 0U);
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, handle.objects().find(decltype(account{}.id){10})),
                     store_plugin::exceptions::stopped);
}

BOOST_AUTO_TEST_CASE(store_plugin_unknown_store_fails_typed) {
   auto app = make_app();
   auto api = app->apis().get<store_plugin::api>(store_plugin::api::ref());

   BOOST_CHECK_THROW(forge::asio::blocking::run(app->runtime(), api->store("missing")),
                     store_plugin::exceptions::unknown_store);
   BOOST_CHECK_THROW(forge::asio::blocking::run(app->runtime(), api->flush("missing", true)),
                     store_plugin::exceptions::unknown_store);

   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

#if FORGE_HAS_ROCKSDB
BOOST_AUTO_TEST_CASE(store_plugin_configured_rocksdb_store_persists_across_reopen) {
   auto root = root_guard{};
   const auto db_path = root.root / "objectdb";

   {
      auto app = make_app(document_for_rocksdb(db_path));
      auto api = app->apis().get<store_plugin::api>(store_plugin::api::ref());
      auto handle = forge::asio::blocking::run(app->runtime(), api->store("accounts"));
      handle.objects().register_object<account_object>();

      forge::asio::blocking::run(app->runtime(), handle.objects().insert(make_account(7, "persisted", 900)));
      forge::asio::blocking::run(app->runtime(), api->flush_all(true));
      forge::asio::blocking::run(app->runtime(), app->shutdown());
   }

   {
      auto app = make_app(document_for_rocksdb(db_path));
      auto api = app->apis().get<store_plugin::api>(store_plugin::api::ref());
      auto handle = forge::asio::blocking::run(app->runtime(), api->store("accounts"));
      handle.objects().register_object<account_object>();

      const auto loaded = forge::asio::blocking::run(app->runtime(), handle.objects().get(decltype(account{}.id){7}));
      BOOST_TEST(loaded.name == "persisted");
      BOOST_TEST(loaded.balance == 900U);

      auto snapshot = forge::asio::blocking::run(app->runtime(), handle.objects().begin_read());
      const auto from_snapshot = forge::asio::blocking::run(app->runtime(), snapshot.get(decltype(account{}.id){7}));
      BOOST_TEST(from_snapshot.name == "persisted");

      forge::asio::blocking::run(app->runtime(), app->shutdown());
   }
}

BOOST_AUTO_TEST_CASE(store_plugin_configured_rocksdb_store_persists_object_and_blob_across_reopen) {
   auto root = root_guard{};
   const auto db_path = root.root / "storedb";

   {
      auto document = forge::config::core::document{};
      document.set("plugins.db.store.stores",
                   forge::config::core::value::array_type{configured_object_blob_store("files", db_path)});
      auto app = make_app(std::move(document));
      auto api = app->apis().get<store_plugin::api>(store_plugin::api::ref());
      auto handle = forge::asio::blocking::run(app->runtime(), api->store("files"));
      handle.objects().register_object<file_object>();

      auto tx = forge::asio::blocking::run(app->runtime(), handle.begin_transaction());
      auto object_tx = handle.objects().join(tx);
      auto blob_tx = handle.blobs().join(tx);

      const auto content = forge::asio::blocking::run(app->runtime(), blob_tx.put(bytes("configured rocksdb blob")));
      forge::asio::blocking::run(app->runtime(), object_tx.insert(make_file(11, "/rocks.txt", content)));
      forge::asio::blocking::run(app->runtime(), tx.commit());
      forge::asio::blocking::run(app->runtime(), api->flush_all(true));
      forge::asio::blocking::run(app->runtime(), app->shutdown());
   }

   {
      auto document = forge::config::core::document{};
      document.set("plugins.db.store.stores",
                   forge::config::core::value::array_type{configured_object_blob_store("files", db_path)});
      auto app = make_app(std::move(document));
      auto api = app->apis().get<store_plugin::api>(store_plugin::api::ref());
      auto handle = forge::asio::blocking::run(app->runtime(), api->store("files"));
      handle.objects().register_object<file_object>();

      const auto loaded = forge::asio::blocking::run(app->runtime(), handle.objects().get(decltype(file_record{}.id){11}));
      BOOST_TEST(loaded.path == "/rocks.txt");
      BOOST_TEST(forge::asio::blocking::run(app->runtime(), handle.blobs().get(loaded.content)).size() == 23U);

      forge::asio::blocking::run(app->runtime(), app->shutdown());
   }
}
#endif

BOOST_AUTO_TEST_SUITE_END()
