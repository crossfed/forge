#include <boost/asio/awaitable.hpp>
#include <boost/test/unit_test.hpp>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

import forge.asio.affine;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.mdbx.driver;

#ifndef FORGE_DB_MDBX_CRASH_HELPER
#define FORGE_DB_MDBX_CRASH_HELPER ""
#endif

namespace {

using namespace std::chrono_literals;

std::filesystem::path make_crash_root(std::string name) {
   const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
   auto root = std::filesystem::temp_directory_path() /
               (std::move(name) + "_" + std::to_string(stamp));
   std::filesystem::remove_all(root);
   std::filesystem::create_directories(root);
   return root;
}

std::vector<std::byte> crash_bytes(std::string value) {
   return {
      reinterpret_cast<const std::byte*>(value.data()),
      reinterpret_cast<const std::byte*>(value.data() + value.size()),
   };
}

std::string crash_text(const std::vector<std::byte>& value) {
   return {
      reinterpret_cast<const char*>(value.data()),
      reinterpret_cast<const char*>(value.data() + value.size()),
   };
}

forge::db::core::record_key crash_key(std::size_t index) {
   return forge::db::core::record_key{
      crash_bytes("entry:" + std::to_string(index))};
}

std::optional<std::size_t> read_acknowledged(
   const std::filesystem::path& root) {
   auto stream = std::ifstream{root / "acknowledged"};
   auto value = std::size_t{};
   if (!(stream >> value)) {
      return std::nullopt;
   }
   return value;
}

pid_t start_crash_helper(const std::filesystem::path& root,
                         std::string durability) {
   BOOST_REQUIRE(std::string_view{FORGE_DB_MDBX_CRASH_HELPER}.size() > 0);
   auto arguments = std::vector<std::string>{
      FORGE_DB_MDBX_CRASH_HELPER,
      root.string(),
      std::move(durability),
      "100000",
   };
   auto native = std::vector<char*>{};
   for (auto& argument : arguments) {
      native.push_back(argument.data());
   }
   native.push_back(nullptr);

   const auto process = ::fork();
   BOOST_REQUIRE(process >= 0);
   if (process == 0) {
      ::execv(native.front(), native.data());
      _exit(127);
   }
   return process;
}

std::size_t stop_after_acknowledgement(pid_t process,
                                       const std::filesystem::path& root) {
   const auto deadline = std::chrono::steady_clock::now() + 10s;
   auto acknowledged = std::optional<std::size_t>{};
   while (std::chrono::steady_clock::now() < deadline) {
      acknowledged = read_acknowledged(root);
      if (acknowledged.value_or(0U) >= 8U) {
         break;
      }
      auto status = int{};
      const auto result = ::waitpid(process, &status, WNOHANG);
      BOOST_REQUIRE_MESSAGE(result == 0,
                            "MDBX crash helper exited before acknowledgement");
      std::this_thread::sleep_for(5ms);
   }
   BOOST_REQUIRE(acknowledged.has_value());
   BOOST_REQUIRE_GE(*acknowledged, 8U);

   BOOST_REQUIRE(::kill(process, SIGKILL) == 0 || errno == ESRCH);
   auto status = int{};
   while (::waitpid(process, &status, 0) < 0) {
      BOOST_REQUIRE(errno == EINTR);
   }
   BOOST_REQUIRE(WIFSIGNALED(status) ||
                 (WIFEXITED(status) && WEXITSTATUS(status) == 0));
   return *acknowledged;
}

void verify_crash_reopen(const std::filesystem::path& root,
                         forge::db::mdbx::durability durability,
                         std::size_t acknowledged,
                         bool require_acknowledged_tail) {
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};
   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await forge::db::mdbx::driver::open(
         {.path = (root / "store").string(),
          .families = {"records"},
          .durability_mode = durability,
          .create_if_missing = false,
          .create_missing_families = false},
         lane.get_executor());
      auto snapshot = co_await driver->begin_read();
      const auto records = forge::db::core::family{"records"};
      auto first_missing = std::optional<std::size_t>{};

      for (auto index = std::size_t{0}; index < acknowledged + 64U; ++index) {
         const auto value = co_await snapshot.get(records, crash_key(index));
         if (!value.has_value()) {
            if (!first_missing.has_value()) {
               first_missing = index;
            }
            continue;
         }
         BOOST_CHECK(!first_missing.has_value());
         BOOST_CHECK_EQUAL(crash_text(*value), "value:" + std::to_string(index));
      }

      if (require_acknowledged_tail) {
         BOOST_CHECK(!first_missing.has_value() || *first_missing >= acknowledged);
      }
      snapshot = {};
      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());
}

void run_crash_case(std::string name, std::string helper_mode,
                    forge::db::mdbx::durability durability,
                    bool require_acknowledged_tail) {
   const auto root = make_crash_root(std::move(name));
   const auto process = start_crash_helper(root, std::move(helper_mode));
   const auto acknowledged = stop_after_acknowledgement(process, root);
   verify_crash_reopen(root, durability, acknowledged,
                       require_acknowledged_tail);
   std::filesystem::remove_all(root);
}

} // namespace

BOOST_AUTO_TEST_SUITE(db_mdbx_crash_test_suite)

BOOST_AUTO_TEST_CASE(db_mdbx_durable_sync_preserves_every_acknowledged_commit) {
   run_crash_case("forge_db_mdbx_crash_durable", "durable",
                  forge::db::mdbx::durability::durable_sync, true);
}

BOOST_AUTO_TEST_CASE(db_mdbx_safe_nosync_reopens_to_a_valid_committed_prefix) {
   run_crash_case("forge_db_mdbx_crash_safe_nosync", "safe-nosync",
                  forge::db::mdbx::durability::safe_nosync, false);
}

BOOST_AUTO_TEST_SUITE_END()
