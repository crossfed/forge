#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <forge/db/object/macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

import forge.asio.affine;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.mdbx.driver;
import forge.db.object.index;
import forge.db.object.object;
import forge.db.object.store;
import forge.db.rocksdb.driver;
import forge.rocksdb.types;

namespace db_backend_benchmark {

struct by_id;
struct by_group;
struct by_bytes;

struct row : forge::db::object::object<row, 91, 1> {
   std::uint32_t group = 0;
   std::uint64_t bytes = 0;
};

BOOST_DESCRIBE_STRUCT(row,
                      (forge::db::object::object<row, 91, 1>),
                      (group, bytes))

using row_index = forge::db::object::object_index<
   row,
   forge::db::object::indexed_by<
      forge::db::object::ranked_primary_unique<
         by_id,
         forge::db::object::ranked_schema<1>,
         forge::db::object::sum<
            by_bytes, forge::db::object::member<&row::bytes>>>,
      forge::db::object::ranked_non_unique<
         by_group,
         forge::db::object::member<&row::group>,
         forge::db::object::ranked_schema<1>,
         forge::db::object::sum<
            by_bytes, forge::db::object::member<&row::bytes>>>>>;

} // namespace db_backend_benchmark

FORGE_DB_OBJECT(db_backend_benchmark::row_index)

namespace {

using clock_type = std::chrono::steady_clock;

constexpr auto record_count = std::size_t{2'000};
constexpr auto query_count = std::size_t{1'000};
constexpr auto write_count = std::size_t{200};

struct benchmark_result {
   std::string name;
   std::chrono::nanoseconds point_reads{};
   std::chrono::nanoseconds scan{};
   std::chrono::nanoseconds commits{};
   std::chrono::nanoseconds savepoints{};
   std::chrono::nanoseconds ranked_queries{};
   std::chrono::nanoseconds concurrent_snapshots{};
};

std::vector<std::byte> bytes(std::string value) {
   return {
      reinterpret_cast<const std::byte*>(value.data()),
      reinterpret_cast<const std::byte*>(value.data() + value.size()),
   };
}

forge::db::core::record_key key(std::string value) {
   return forge::db::core::record_key{bytes(std::move(value))};
}

void require(bool condition, std::string_view message) {
   if (!condition) {
      throw std::runtime_error{std::string{message}};
   }
}

boost::asio::awaitable<benchmark_result> run_benchmark(
   std::string name,
   const std::shared_ptr<forge::db::core::driver>& driver) {
   const auto records = forge::db::core::family{"records"};
   {
      auto seed = co_await driver->begin_transaction();
      for (auto index = std::size_t{0}; index < record_count; ++index) {
         co_await seed.put(records, key("record:" + std::to_string(index)),
                           bytes("value:" + std::to_string(index)));
      }
      co_await seed.commit();
   }

   auto objects = co_await forge::db::object::store::open(
      driver,
      forge::db::object::store::options{
         .writes = forge::db::object::write_policy::backend});
   objects.register_object<db_backend_benchmark::row_index>();
   {
      auto seed = co_await objects.begin_transaction();
      for (auto index = std::size_t{0}; index < record_count; ++index) {
         auto value = db_backend_benchmark::row{};
         value.id = db_backend_benchmark::row::id_t{index};
         value.group = static_cast<std::uint32_t>(index % 32U);
         value.bytes = index + 1U;
         co_await seed.insert(value);
      }
      co_await seed.commit();
   }

   auto result = benchmark_result{.name = std::move(name)};
   auto snapshot = co_await driver->begin_read();
   auto started = clock_type::now();
   for (auto index = std::size_t{0}; index < query_count; ++index) {
      require((co_await snapshot.get(
                  records, key("record:" + std::to_string(index % record_count))))
                 .has_value(),
              "point read missed a seeded record");
   }
   result.point_reads = clock_type::now() - started;

   started = clock_type::now();
   auto cursor = std::optional<forge::db::core::cursor>{};
   auto scanned = std::size_t{0};
   do {
      auto page = co_await snapshot.scan_page(
         records,
         forge::db::core::record_range{.has_end = false},
         forge::db::core::page_request{.after = cursor, .limit = 256});
      scanned += page.entries.size();
      cursor = std::move(page.next);
   } while (cursor.has_value());
   result.scan = clock_type::now() - started;
   require(scanned == record_count, "scan did not return every seeded record");
   snapshot = {};

   started = clock_type::now();
   for (auto index = std::size_t{0}; index < write_count; ++index) {
      auto transaction = co_await driver->begin_transaction();
      co_await transaction.put(records, key("commit:" + std::to_string(index)),
                               bytes("committed"));
      co_await transaction.commit();
   }
   result.commits = clock_type::now() - started;

   started = clock_type::now();
   for (auto index = std::size_t{0}; index < write_count; ++index) {
      auto transaction = co_await driver->begin_transaction();
      const auto point = co_await transaction.create_savepoint();
      co_await transaction.put(records,
                               key("savepoint:" + std::to_string(index)),
                               bytes("rolled-back"));
      co_await transaction.rollback_to_savepoint(point);
      co_await transaction.commit();
   }
   result.savepoints = clock_type::now() - started;

   auto primary = objects.index<db_backend_benchmark::row_index,
                                db_backend_benchmark::by_id>();
   auto groups = objects.index<db_backend_benchmark::row_index,
                               db_backend_benchmark::by_group>();
   started = clock_type::now();
   for (auto index = std::size_t{0}; index < query_count; ++index) {
      require(co_await primary.count() == record_count,
              "ranked count changed during benchmark");
      static_cast<void>(co_await primary.sum<db_backend_benchmark::by_bytes>());
      require((co_await primary.nth(index % record_count)).has_value(),
              "ranked nth missed a seeded object");
      static_cast<void>(co_await groups.lower_bound_rank(
         static_cast<std::uint32_t>(index % 32U)));
   }
   result.ranked_queries = clock_type::now() - started;

   auto shared = co_await driver->begin_read();
   started = clock_type::now();
   auto completed = std::atomic_size_t{0};
   auto threads = std::vector<std::thread>{};
   for (auto thread_index = std::size_t{0}; thread_index < 4U; ++thread_index) {
      threads.emplace_back([shared, records, thread_index, &completed]() mutable {
         auto runtime = forge::asio::runtime{};
         forge::asio::blocking::run(
            runtime,
            [shared = std::move(shared), records, thread_index,
             &completed]() mutable -> boost::asio::awaitable<void> {
               for (auto offset = std::size_t{0}; offset < query_count / 4U;
                    ++offset) {
                  const auto index = (thread_index * (query_count / 4U) + offset) %
                                     record_count;
                  if ((co_await shared.get(
                          records, key("record:" + std::to_string(index))))
                         .has_value()) {
                     completed.fetch_add(1U, std::memory_order_relaxed);
                  }
               }
            }());
      });
   }
   for (auto& thread : threads) {
      thread.join();
   }
   result.concurrent_snapshots = clock_type::now() - started;
   require(completed.load(std::memory_order_relaxed) == query_count,
           "concurrent snapshot read missed a seeded record");
   co_return result;
}

double operations_per_second(std::size_t operations,
                             std::chrono::nanoseconds elapsed) {
   return static_cast<double>(operations) /
          std::chrono::duration<double>{elapsed}.count();
}

void print_result(const benchmark_result& result) {
   std::cout << result.name << '\n'
             << "  point reads: "
             << operations_per_second(query_count, result.point_reads) << " ops/s\n"
             << "  full scans:  "
             << operations_per_second(record_count, result.scan) << " rows/s\n"
             << "  commits:     "
             << operations_per_second(write_count, result.commits) << " ops/s\n"
             << "  savepoints:  "
             << operations_per_second(write_count, result.savepoints) << " ops/s\n"
             << "  ranked:      "
             << operations_per_second(query_count, result.ranked_queries) << " groups/s\n"
             << "  snapshots:   "
             << operations_per_second(query_count, result.concurrent_snapshots)
             << " reads/s\n";
}

} // namespace

int main() try {
   const auto root = std::filesystem::temp_directory_path() /
                     ("forge_db_backend_benchmark_" +
                      std::to_string(clock_type::now().time_since_epoch().count()));
   std::filesystem::remove_all(root);
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{{.thread_name = "mdbx-benchmark"}};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto mdbx = co_await forge::db::mdbx::driver::open(
         {.path = (root / "mdbx").string(),
          .families = {"objectdb", "records"},
          .map = {.upper_size = 4ULL * 1024 * 1024 * 1024,
                  .growth_step = 64ULL * 1024 * 1024}},
         lane.get_executor());
      print_result(co_await run_benchmark("MDBX", mdbx));
      co_await mdbx->async_close();
      mdbx.reset();
      co_await lane.shutdown();

      auto rocksdb = std::make_shared<forge::db::rocksdb::driver>(
         forge::db::rocksdb::config{
            .path = (root / "rocksdb").string(),
            .families = {
               forge::rocksdb::column_family_config{"objectdb"},
               forge::rocksdb::column_family_config{"records"},
            }});
      print_result(co_await run_benchmark("RocksDB", rocksdb));
      co_await rocksdb->async_close();
   }());

   std::filesystem::remove_all(root);
   return 0;
} catch (const std::exception& error) {
   std::cerr << "benchmark failed: " << error.what() << '\n';
   return 1;
}
