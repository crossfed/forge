#include <boost/test/unit_test.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

import forge.asio.blocking;
import forge.asio.runtime;
import forge.crypto.asymmetric;
import forge.crypto.core.secret_string;
import forge.crypto.digest.sha256;
import forge.crypto.signer.configured_provider;

namespace signer = forge::crypto::signer;

namespace {

struct temporary_directory {
   temporary_directory() {
      const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
      path = std::filesystem::temp_directory_path() / ("forge-configured-signer-" + std::to_string(suffix));
      std::filesystem::create_directories(path);
      std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
   }

   ~temporary_directory() {
      auto error = std::error_code{};
      std::filesystem::remove_all(path, error);
   }

   std::filesystem::path path;
};

void write_key_file(const std::filesystem::path& path, const std::string& value,
                    std::filesystem::perms permissions = std::filesystem::perms::owner_read) {
   if (std::filesystem::exists(path)) {
      std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                   std::filesystem::perm_options::replace);
   }
   auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
   BOOST_REQUIRE(output.good());
   output.write(value.data(), static_cast<std::streamsize>(value.size()));
   output.close();
   BOOST_REQUIRE(output.good());
   std::filesystem::permissions(path, permissions, std::filesystem::perm_options::replace);
}

struct key_fixture {
   forge::crypto::asymmetric::private_key private_key = forge::crypto::asymmetric::private_key::generate();
   std::string encoded = forge::crypto::asymmetric::encoding::antelope().format(private_key);
   forge::crypto::asymmetric::public_key public_key = private_key.get_public_key();
};

} // namespace

BOOST_AUTO_TEST_SUITE(crypto_configured_signer_tests)

BOOST_AUTO_TEST_CASE(inline_key_exposes_and_uses_exact_configured_id) {
   auto fixture = key_fixture{};
   auto value = signer::configured_provider::from_private_key({.value = "account"},
                                                              forge::crypto::core::secret_string{fixture.encoded},
                                                              forge::crypto::asymmetric::encoding::antelope());
   auto runtime = forge::asio::runtime{};

   const auto keys = forge::asio::blocking::run(runtime, value->keys());
   BOOST_REQUIRE_EQUAL(keys.size(), 1U);
   BOOST_TEST(keys.front().id.value == "account");
   BOOST_TEST(keys.front().public_key == fixture.public_key);

   const auto digest = forge::crypto::digest::sha256::hash(std::string{"configured-provider"});
   const auto signed_digest =
       forge::asio::blocking::run(runtime, value->sign_digest({.id = {.value = "account"}, .digest = digest}));
   BOOST_TEST(signed_digest.public_key == fixture.public_key);
   BOOST_TEST(forge::crypto::asymmetric::recover(signed_digest.signature, digest) == fixture.public_key);

   BOOST_CHECK_THROW((void)forge::asio::blocking::run(runtime, value->describe({.value = "other"})),
                     signer::exceptions::unknown_key);
   BOOST_CHECK_THROW(
       (void)forge::asio::blocking::run(runtime, value->sign_digest({.id = {.value = "other"}, .digest = digest})),
       signer::exceptions::unknown_key);
}

BOOST_AUTO_TEST_CASE(private_file_accepts_exact_mode_and_one_line_ending) {
   auto fixture = key_fixture{};
   auto directory = temporary_directory{};
   const auto path = directory.path / "private-key";
   write_key_file(path, fixture.encoded + "\r\n");

#if !defined(_WIN32)
   auto value = signer::configured_provider::from_private_key_file({.value = "receipt"}, path, {},
                                                                   forge::crypto::asymmetric::encoding::antelope());
   auto runtime = forge::asio::runtime{};
   const auto info = forge::asio::blocking::run(runtime, value->describe({.value = "receipt"}));
   BOOST_TEST(info.public_key == fixture.public_key);
#else
   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key_file(
                         {.value = "receipt"}, path, {}, forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::unavailable);
#endif
}

BOOST_AUTO_TEST_CASE(private_file_rejects_unbounded_or_insecure_sources) {
   auto fixture = key_fixture{};
   auto directory = temporary_directory{};
   const auto path = directory.path / "private-key";

   write_key_file(path, fixture.encoded, std::filesystem::perms::owner_read | std::filesystem::perms::group_read);
   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key_file(
                         {.value = "key"}, path, {.max_bytes = 0}, forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::invalid_config);
#if !defined(_WIN32)
   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key_file(
                         {.value = "key"}, path, {}, forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::insecure_permissions);

   write_key_file(path, fixture.encoded, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key_file(
                         {.value = "key"}, path, {}, forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::insecure_permissions);

   const auto symlink = directory.path / "private-key-link";
   std::filesystem::create_symlink(path.filename(), symlink);
   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key_file(
                         {.value = "key"}, symlink, {}, forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::insecure_permissions);

   const auto fifo = directory.path / "private-key-fifo";
   BOOST_REQUIRE_EQUAL(::mkfifo(fifo.c_str(), 0400), 0);
   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key_file(
                         {.value = "key"}, fifo, {}, forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::insecure_permissions);

   write_key_file(path, fixture.encoded);
   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key_file(
                         {.value = "key"}, path, {.max_bytes = fixture.encoded.size() - 1U},
                         forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::source_too_large);

   auto invalid = std::string{"PVT_K1_invalid"};
   invalid.push_back('\0');
   invalid.append("secret");
   write_key_file(path, invalid);
   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key_file(
                         {.value = "key"}, path, {}, forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::invalid_key);

   BOOST_CHECK_THROW(
       (void)signer::configured_provider::from_private_key_file({.value = "key"}, directory.path / "missing", {},
                                                                forge::crypto::asymmetric::encoding::antelope()),
       signer::exceptions::io_error);
#else
   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key_file(
                         {.value = "key"}, path, {}, forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::unavailable);
#endif
}

BOOST_AUTO_TEST_CASE(inline_source_rejects_empty_id_and_redacts_invalid_key) {
   BOOST_CHECK_THROW((void)signer::configured_provider::create({.id = {.value = "key"}}),
                     signer::exceptions::invalid_config);

   auto fixture = key_fixture{};
   auto ambiguous = signer::configured_provider_options{
       .id = {.value = "key"},
       .private_key = forge::crypto::core::secret_string{fixture.encoded},
       .private_key_file = std::filesystem::path{"private-key"},
   };
   BOOST_CHECK_THROW(
       (void)signer::configured_provider::create(std::move(ambiguous), forge::crypto::asymmetric::encoding::antelope()),
       signer::exceptions::invalid_config);

   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key(
                         {}, forge::crypto::core::secret_string{"PVT_K1_secret-value"},
                         forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::invalid_config);

   BOOST_CHECK_THROW(
       (void)signer::configured_provider::from_private_key({.value = "key"}, forge::crypto::core::secret_string{},
                                                           forge::crypto::asymmetric::encoding::antelope()),
       signer::exceptions::invalid_source);

   try {
      (void)signer::configured_provider::from_private_key({.value = "key"},
                                                          forge::crypto::core::secret_string{"PVT_K1_secret-value"},
                                                          forge::crypto::asymmetric::encoding::antelope());
      BOOST_FAIL("invalid private key was accepted");
   } catch (const signer::exceptions::invalid_key& error) {
      BOOST_TEST(std::string{error.what()}.find("secret-value") == std::string::npos);
   }

   BOOST_CHECK_THROW((void)signer::configured_provider::from_private_key(
                         {.value = "key"}, forge::crypto::core::secret_string{std::string(4097, 'x')},
                         forge::crypto::asymmetric::encoding::antelope()),
                     signer::exceptions::source_too_large);
}

BOOST_AUTO_TEST_SUITE_END()
