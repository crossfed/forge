#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

import forge.asio.blocking;
import forge.asio.compute;
import forge.asio.runtime;
import forge.auth.oauth2.access_token;
import forge.auth.workload.projected_token_file;

namespace {

namespace oauth2 = forge::auth::oauth2;
namespace workload = forge::auth::workload;

template <typename Value>
concept member_serializable = requires(const Value& value) { value.serialize(); };

template <typename Value>
concept stream_insertable = requires(std::ostream& stream, const Value& value) { stream << value; };

static_assert(!std::is_copy_constructible_v<oauth2::access_token>);
static_assert(!std::is_copy_assignable_v<oauth2::access_token>);
static_assert(std::is_nothrow_move_constructible_v<oauth2::access_token>);
static_assert(std::is_nothrow_move_assignable_v<oauth2::access_token>);
static_assert(std::same_as<decltype(std::declval<const oauth2::access_token&>().secret()),
                           const forge::crypto::core::secret_string&>);
static_assert(!boost::describe::has_describe_members<oauth2::access_token>::value);
static_assert(!boost::describe::has_describe_members<forge::crypto::core::secret_string>::value);
static_assert(!member_serializable<oauth2::access_token>);
static_assert(!member_serializable<forge::crypto::core::secret_string>);
static_assert(!stream_insertable<oauth2::access_token>);
static_assert(!stream_insertable<forge::crypto::core::secret_string>);

class temporary_directory {
 public:
   temporary_directory() {
      const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
      const auto root =
#if defined(__APPLE__)
          std::filesystem::path{"/private/tmp"};
#else
          std::filesystem::temp_directory_path();
#endif
      path_ = root / ("forge-client-auth-" + std::to_string(suffix) + "-" +
                      std::to_string(reinterpret_cast<std::uintptr_t>(this)));
      std::filesystem::create_directories(path_);
   }

   ~temporary_directory() {
      auto error = std::error_code{};
      std::filesystem::remove_all(path_, error);
   }

   temporary_directory(const temporary_directory&) = delete;
   temporary_directory& operator=(const temporary_directory&) = delete;

   [[nodiscard]] const std::filesystem::path& path() const noexcept {
      return path_;
   }

 private:
   std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, std::string_view value) {
   auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
   BOOST_REQUIRE(output.good());
   output.write(value.data(), static_cast<std::streamsize>(value.size()));
   output.close();
   BOOST_REQUIRE(output.good());
}

class workload_reader {
 public:
   workload_reader()
       : runtime_{forge::asio::runtime_options{.worker_threads = 1}},
         pool_{forge::asio::compute::pool::options{.worker_threads = 1, .thread_name = "forge-auth-file-test"}} {}

   [[nodiscard]] workload::subject_token read(workload::projected_token_file_options options) {
      auto source = workload::projected_token_file{pool_.get_executor(), std::move(options)};
      return forge::asio::blocking::run(runtime_, source.async_subject_token());
   }

   [[nodiscard]] forge::asio::compute::metrics metrics() const {
      return pool_.snapshot();
   }

   void shutdown() {
      forge::asio::blocking::run(runtime_, pool_.shutdown());
   }

 private:
   forge::asio::runtime runtime_;
   forge::asio::compute::pool pool_;
};

} // namespace

BOOST_AUTO_TEST_SUITE(client_auth_tests)

BOOST_AUTO_TEST_CASE(access_token_owns_a_move_only_secret_string) {
   auto token = oauth2::access_token{
       forge::crypto::core::secret_string{std::string{"access-token-secret"}},
       {.issuer = "https://issuer.example"},
   };
   auto moved = std::move(token);

   BOOST_CHECK(moved.secret().view() == std::string_view{"access-token-secret"});
   BOOST_CHECK_EQUAL(moved.metadata().issuer, "https://issuer.example");
}

BOOST_AUTO_TEST_CASE(projected_token_file_validates_bounded_options_and_executor) {
   auto pool = forge::asio::compute::pool{forge::asio::compute::pool::options{.worker_threads = 1}};
   const auto executor = pool.get_executor();
   const auto expect_invalid = [&executor](workload::projected_token_file_options options) {
      BOOST_CHECK_THROW((void)workload::projected_token_file(executor, std::move(options)),
                        workload::exceptions::invalid_options);
   };

   expect_invalid({.path = {}});
   expect_invalid({.path = "token", .max_bytes = 0});
   expect_invalid({.path = "token", .max_bytes = 1024U * 1024U + 1U});
   expect_invalid({.path = "token", .max_rotation_attempts = 0});
   expect_invalid({.path = "token", .max_rotation_attempts = 17});

   const auto valid_options = workload::projected_token_file_options{.path = "token"};
   BOOST_CHECK_THROW((void)workload::projected_token_file(forge::asio::compute::executor{}, valid_options),
                     workload::exceptions::invalid_options);
}

#if !defined(_WIN32)
BOOST_AUTO_TEST_CASE(projected_token_file_reads_on_compute_and_observes_atomic_rotation) {
   auto directory = temporary_directory{};
   const auto token_path = directory.path() / "token";
   const auto replacement_path = directory.path() / "token.next";
   write_file(token_path, "first-token");

   auto reader = workload_reader{};
   auto first = reader.read({.path = token_path});
   BOOST_CHECK(first.secret().view() == std::string_view{"first-token"});
   BOOST_CHECK_EQUAL(reader.metrics().submitted, 1U);
   BOOST_CHECK_EQUAL(reader.metrics().completed, 1U);

   write_file(replacement_path, "rotated-token");
   std::filesystem::rename(replacement_path, token_path);

   auto rotated = reader.read({.path = token_path});
   BOOST_CHECK(rotated.secret().view() == std::string_view{"rotated-token"});
   BOOST_CHECK_EQUAL(reader.metrics().submitted, 2U);
   BOOST_CHECK_EQUAL(reader.metrics().completed, 2U);
   reader.shutdown();
}

BOOST_AUTO_TEST_CASE(projected_token_file_rejects_symlink_and_non_regular_sources) {
   auto directory = temporary_directory{};
   const auto token_path = directory.path() / "token";
   const auto symlink_path = directory.path() / "token-link";
   const auto non_regular_path = directory.path() / "token-directory";
   write_file(token_path, "subject-token");
   std::filesystem::create_symlink(token_path, symlink_path);
   std::filesystem::create_directory(non_regular_path);

   auto reader = workload_reader{};
   BOOST_CHECK_THROW((void)reader.read({.path = symlink_path}), workload::exceptions::insecure_source);
   BOOST_CHECK_THROW((void)reader.read({.path = non_regular_path}), workload::exceptions::insecure_source);
   reader.shutdown();
}

BOOST_AUTO_TEST_CASE(projected_token_file_rejects_ancestor_symlink_replacement) {
   auto directory = temporary_directory{};
   const auto source_directory = directory.path() / "source";
   const auto source_backup = directory.path() / "source.backup";
   std::filesystem::create_directories(source_directory);
   write_file(source_directory / "token", "subject-token");
   std::filesystem::rename(source_directory, source_backup);
   std::filesystem::create_directory_symlink(source_backup, source_directory);

   auto reader = workload_reader{};
   BOOST_CHECK_THROW((void)reader.read({.path = source_directory / "token"}), workload::exceptions::insecure_source);
   reader.shutdown();
}

BOOST_AUTO_TEST_CASE(projected_token_file_rejects_oversize_and_empty_inputs) {
   auto directory = temporary_directory{};
   const auto oversize_path = directory.path() / "oversize-token";
   const auto empty_path = directory.path() / "empty-token";
   write_file(oversize_path, "123456789");
   write_file(empty_path, "");

   auto reader = workload_reader{};
   BOOST_CHECK_THROW((void)reader.read({.path = oversize_path, .max_bytes = 8}),
                     workload::exceptions::source_too_large);
   BOOST_CHECK_THROW((void)reader.read({.path = empty_path}), workload::exceptions::token_unavailable);
   reader.shutdown();
}
#endif

BOOST_AUTO_TEST_SUITE_END()
