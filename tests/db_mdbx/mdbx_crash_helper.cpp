#include <boost/asio/awaitable.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

import forge.asio.affine;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.mdbx.driver;

namespace {

std::vector<std::byte> bytes(std::string value) {
   return {
      reinterpret_cast<const std::byte*>(value.data()),
      reinterpret_cast<const std::byte*>(value.data() + value.size()),
   };
}

forge::db::core::record_key entry_key(std::size_t index) {
   return forge::db::core::record_key{bytes("entry:" + std::to_string(index))};
}

void require_system_call(int result, std::string_view operation) {
   if (result < 0) {
      throw std::system_error{errno, std::generic_category(), std::string{operation}};
   }
}

void write_all(int descriptor, std::string_view value) {
   auto written = std::size_t{0};
   while (written < value.size()) {
      const auto result = ::write(descriptor, value.data() + written,
                                  value.size() - written);
      if (result < 0 && errno == EINTR) {
         continue;
      }
      require_system_call(static_cast<int>(result), "write acknowledgement");
      written += static_cast<std::size_t>(result);
   }
}

void acknowledge(const std::filesystem::path& root, std::size_t count) {
   const auto target = root / "acknowledged";
   const auto temporary = root / "acknowledged.tmp";
   const auto value = std::to_string(count);

   const auto descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                  S_IRUSR | S_IWUSR);
   require_system_call(descriptor, "open acknowledgement");
   try {
      write_all(descriptor, value);
      require_system_call(::fsync(descriptor), "sync acknowledgement");
   } catch (...) {
      static_cast<void>(::close(descriptor));
      throw;
   }
   require_system_call(::close(descriptor), "close acknowledgement");
   require_system_call(::rename(temporary.c_str(), target.c_str()),
                       "publish acknowledgement");

   auto directory_flags = O_RDONLY;
#if defined(O_DIRECTORY)
   directory_flags |= O_DIRECTORY;
#endif
   const auto directory = ::open(root.c_str(), directory_flags);
   require_system_call(directory, "open acknowledgement directory");
   try {
      require_system_call(::fsync(directory), "sync acknowledgement directory");
   } catch (...) {
      static_cast<void>(::close(directory));
      throw;
   }
   require_system_call(::close(directory), "close acknowledgement directory");
}

forge::db::mdbx::durability parse_durability(std::string_view value) {
   if (value == "durable") {
      return forge::db::mdbx::durability::durable_sync;
   }
   if (value == "safe-nosync") {
      return forge::db::mdbx::durability::safe_nosync;
   }
   throw std::invalid_argument{"unknown MDBX durability mode"};
}

} // namespace

int main(int argc, char** argv) try {
   if (argc != 4) {
      return 2;
   }

   const auto root = std::filesystem::path{argv[1]};
   const auto mode = parse_durability(argv[2]);
   const auto commits = static_cast<std::size_t>(std::stoull(argv[3]));
   std::filesystem::create_directories(root);

   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{{.thread_name = "mdbx-crash"}};
   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await forge::db::mdbx::driver::open(
         {.path = (root / "store").string(),
          .families = {"records"},
          .durability_mode = mode},
         lane.get_executor());
      const auto records = forge::db::core::family{"records"};

      for (auto index = std::size_t{0}; index < commits; ++index) {
         auto transaction = co_await driver->begin_transaction();
         co_await transaction.put(records, entry_key(index),
                                  bytes("value:" + std::to_string(index)));
         co_await transaction.commit();
         acknowledge(root, index + 1U);
      }
   }());

   std::_Exit(0);
} catch (...) {
   return 1;
}
