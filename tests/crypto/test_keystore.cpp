#include <boost/test/unit_test.hpp>

#include <sys/stat.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import forge.asio.blocking;
import forge.asio.runtime;
import forge.crypto.asymmetric;
import forge.crypto.core.random;
import forge.crypto.keystore.encrypted_file;
import forge.crypto.keystore.password;
import forge.crypto.keystore.store;
import forge.raw.raw;

namespace keystore = forge::crypto::keystore;

namespace {

struct temporary_directory {
   temporary_directory() {
      path = std::filesystem::temp_directory_path() /
             ("forge-keystore-test-" + std::to_string(forge::crypto::core::random_array<8>()[0]) + "-" +
              std::to_string(reinterpret_cast<std::uintptr_t>(this)));
      std::filesystem::create_directories(path);
      std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
   }

   ~temporary_directory() {
      auto error = std::error_code{};
      std::filesystem::remove_all(path, error);
   }

   std::filesystem::path path;
};

forge::crypto::core::secret_string password(std::string value = "correct horse battery staple") {
   return forge::crypto::core::secret_string{std::move(value)};
}

void write_private_file(const std::filesystem::path& path, const forge::crypto::core::bytes& bytes) {
   auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
   output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
   output.close();
   std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                std::filesystem::perm_options::replace);
}

void overwrite_u64(forge::crypto::core::bytes& bytes, std::size_t offset, std::uint64_t value) {
   BOOST_REQUIRE(bytes.size() >= offset + 8U);
   for (auto index = 0U; index < 8U; ++index) {
      bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
   }
}

} // namespace

BOOST_AUTO_TEST_SUITE(crypto_keystore_tests)

BOOST_AUTO_TEST_CASE(encrypted_file_authenticates_header_ciphertext_and_password) {
   auto plaintext = forge::crypto::core::bytes{'s', 'e', 'c', 'r', 'e', 't'};
   auto container = keystore::encrypt_file({
       .plaintext = forge::crypto::core::secret_bytes{plaintext},
       .password = password(),
   });
   const auto second_container = keystore::encrypt_file({
       .plaintext = forge::crypto::core::secret_bytes{plaintext},
       .password = password(),
   });

   BOOST_TEST(container != second_container);
   BOOST_TEST(keystore::decrypt_file(container, password()).copy() == plaintext);
   BOOST_CHECK_THROW((void)keystore::decrypt_file(container, password("wrong")), keystore::exceptions::invalid_file);

   auto tampered = container;
   tampered.back() ^= 0x01U;
   BOOST_CHECK_THROW((void)keystore::decrypt_file(tampered, password()), keystore::exceptions::invalid_file);

   auto truncated_header = container;
   truncated_header.resize(9U);
   BOOST_CHECK_THROW((void)keystore::decrypt_file(truncated_header, password()), keystore::exceptions::invalid_file);

   auto truncated_body = container;
   truncated_body.pop_back();
   BOOST_CHECK_THROW((void)keystore::decrypt_file(truncated_body, password()), keystore::exceptions::invalid_file);

   auto trailing = container;
   trailing.push_back(0xffU);
   BOOST_CHECK_THROW((void)keystore::decrypt_file(trailing, password()), keystore::exceptions::invalid_file);

   BOOST_CHECK_THROW((void)keystore::decrypt_file(
                         container, password(), keystore::decrypt_limits{.max_plaintext_bytes = plaintext.size() - 1U}),
                     keystore::exceptions::size_limit_exceeded);
}

BOOST_AUTO_TEST_CASE(encrypted_file_rejects_invalid_shape_and_kdf_parameters_before_derivation) {
   auto container = keystore::encrypt_file({
       .plaintext = forge::crypto::core::secret_bytes{forge::crypto::core::bytes{'x'}},
       .password = password(),
   });
   container[8U] = 0xffU;

   BOOST_CHECK_THROW((void)keystore::decrypt_file(container, password()), keystore::exceptions::invalid_file);

   auto invalid_n = keystore::encrypt_file({
       .plaintext = forge::crypto::core::secret_bytes{forge::crypto::core::bytes{'x'}},
       .password = password(),
   });
   overwrite_u64(invalid_n, 8U, 16'385U);
   BOOST_CHECK_THROW((void)keystore::decrypt_file(invalid_n, password()), keystore::exceptions::invalid_file);

   auto invalid_salt = keystore::encrypt_file({
       .plaintext = forge::crypto::core::secret_bytes{forge::crypto::core::bytes{'x'}},
       .password = password(),
   });
   overwrite_u64(invalid_salt, 40U, 15U);
   BOOST_CHECK_THROW((void)keystore::decrypt_file(invalid_salt, password()), keystore::exceptions::invalid_file);

   auto invalid_nonce = keystore::encrypt_file({
       .plaintext = forge::crypto::core::secret_bytes{forge::crypto::core::bytes{'x'}},
       .password = password(),
   });
   overwrite_u64(invalid_nonce, 48U, 0U);
   BOOST_CHECK_THROW((void)keystore::decrypt_file(invalid_nonce, password()), keystore::exceptions::invalid_file);
}

BOOST_AUTO_TEST_CASE(store_is_atomic_private_and_reopens_for_signing) {
   auto directory = temporary_directory{};
   const auto path = directory.path / "keys.fks";
   const auto private_key = forge::crypto::asymmetric::private_key::generate();
   const auto public_key = private_key.get_public_key();

   {
      auto value = keystore::store::create(path, password());
      value.put({.value = "devnet"}, private_key);
   }

   struct stat status{};
   BOOST_REQUIRE(::stat(path.c_str(), &status) == 0);
   BOOST_TEST((status.st_mode & 0777U) == 0600U);

   auto value = keystore::store::open(path, password());
   auto runtime = forge::asio::runtime{};
   const auto info = forge::asio::blocking::run(runtime, value.describe({.value = "devnet"}));
   BOOST_TEST(info.public_key == public_key);

   const auto digest = forge::crypto::digest::sha256::hash(std::string{"payload"});
   const auto signed_digest =
       forge::asio::blocking::run(runtime, value.sign_digest({.id = {.value = "devnet"}, .digest = digest}));
   BOOST_TEST(signed_digest.public_key == public_key);
   BOOST_TEST(forge::crypto::asymmetric::recover(signed_digest.signature, digest) == public_key);
}

BOOST_AUTO_TEST_CASE(store_creates_private_durable_directory_chain) {
   auto directory = temporary_directory{};
   const auto first = directory.path / "nested";
   const auto second = first / "keystore";
   const auto path = second / "keys.fks";

   auto value = keystore::store::create(path, password());
   value.put({.value = "devnet"}, forge::crypto::asymmetric::private_key::generate());

   for (const auto& component : {first, second}) {
      struct stat status{};
      BOOST_REQUIRE(::lstat(component.c_str(), &status) == 0);
      BOOST_TEST(S_ISDIR(status.st_mode));
      BOOST_TEST((status.st_mode & 0777U) == 0700U);
   }
   auto reopened = keystore::store::open(path, password());
   auto runtime = forge::asio::runtime{};
   BOOST_TEST(forge::asio::blocking::run(runtime, reopened.keys()).size() == 1U);
}

BOOST_AUTO_TEST_CASE(store_resynchronizes_a_preexisting_private_directory_chain) {
   auto directory = temporary_directory{};
   const auto first = directory.path / "nested";
   const auto second = first / "keystore";
   std::filesystem::create_directories(second);
   std::filesystem::permissions(first, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
   std::filesystem::permissions(second, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);

   const auto path = second / "keys.fks";
   auto value = keystore::store::create(path, password());
   value.put({.value = "devnet"}, forge::crypto::asymmetric::private_key::generate());

   auto reopened = keystore::store::open(path, password());
   auto runtime = forge::asio::runtime{};
   BOOST_TEST(forge::asio::blocking::run(runtime, reopened.keys()).size() == 1U);
}

BOOST_AUTO_TEST_CASE(store_rejects_duplicate_ids_and_tampered_files) {
   auto directory = temporary_directory{};
   const auto path = directory.path / "keys.fks";
   auto value = keystore::store::create(path, password());
   value.put({.value = "key"}, forge::crypto::asymmetric::private_key::generate());
   BOOST_CHECK_THROW(value.put({.value = "key"}, forge::crypto::asymmetric::private_key::generate()),
                     keystore::exceptions::duplicate_key);

   auto data = std::ifstream{path, std::ios::binary};
   auto bytes = std::string{std::istreambuf_iterator<char>{data}, std::istreambuf_iterator<char>{}};
   bytes.back() ^= 0x01;
   auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
   output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
   output.close();
   BOOST_CHECK_THROW((void)keystore::store::open(path, password()), keystore::exceptions::invalid_file);
}

BOOST_AUTO_TEST_CASE(store_translates_authenticated_malformed_entries_to_invalid_file) {
   auto directory = temporary_directory{};
   const auto path = directory.path / "keys.fks";

   for (const auto& entries : std::vector<std::vector<std::pair<std::string, std::string>>>{
            {{"", "not-a-private-key"}},
            {{"valid-id", "not-a-private-key"}},
        }) {
      auto packed = forge::raw::pack(entries);
      auto container = keystore::encrypt_file({
          .plaintext = forge::crypto::core::secret_bytes{std::move(packed)},
          .password = password(),
      });
      write_private_file(path, container);
      BOOST_CHECK_THROW((void)keystore::store::open(path, password()), keystore::exceptions::invalid_file);
   }
}

BOOST_AUTO_TEST_CASE(store_provider_uses_signer_unknown_key_contract) {
   auto directory = temporary_directory{};
   auto value = keystore::store::create(directory.path / "keys.fks", password());
   auto runtime = forge::asio::runtime{};
   const auto missing = forge::crypto::signer::key_id{.value = "missing"};

   BOOST_CHECK_THROW((void)forge::asio::blocking::run(runtime, value.describe(missing)),
                     forge::crypto::signer::exceptions::unknown_key);
   BOOST_CHECK_THROW(
       (void)forge::asio::blocking::run(
           runtime,
           value.sign_digest({.id = missing, .digest = forge::crypto::digest::sha256::hash(std::string{"payload"})})),
       forge::crypto::signer::exceptions::unknown_key);
}

BOOST_AUTO_TEST_CASE(store_serializes_concurrent_updates_and_rolls_back_failed_persistence) {
   auto directory = temporary_directory{};
   const auto path = directory.path / "keys.fks";
   auto value = keystore::store::create(path, password());

   auto workers = std::vector<std::thread>{};
   for (auto index = 0U; index < 4U; ++index) {
      workers.emplace_back([&value, index] {
         value.put({.value = "key-" + std::to_string(index)}, forge::crypto::asymmetric::private_key::generate());
      });
   }
   for (auto& worker : workers) {
      worker.join();
   }

   auto runtime = forge::asio::runtime{};
   BOOST_TEST(forge::asio::blocking::run(runtime, value.keys()).size() == 4U);

   std::filesystem::permissions(directory.path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                std::filesystem::perm_options::replace);
   BOOST_CHECK_THROW(value.put({.value = "not-persisted"}, forge::crypto::asymmetric::private_key::generate()),
                     keystore::exceptions::io_error);
   BOOST_CHECK_THROW((void)forge::asio::blocking::run(runtime, value.describe({.value = "not-persisted"})),
                     forge::crypto::signer::exceptions::unknown_key);
   std::filesystem::permissions(directory.path, std::filesystem::perms::owner_all,
                                std::filesystem::perm_options::replace);
}

BOOST_AUTO_TEST_CASE(store_rejects_insecure_directory_permissions) {
   auto directory = temporary_directory{};
   std::filesystem::permissions(directory.path, std::filesystem::perms::owner_all | std::filesystem::perms::group_read,
                                std::filesystem::perm_options::replace);
   BOOST_CHECK_THROW((void)keystore::store::create(directory.path / "keys.fks", password()),
                     keystore::exceptions::invalid_file);
   std::filesystem::permissions(directory.path, std::filesystem::perms::owner_all,
                                std::filesystem::perm_options::replace);
}

BOOST_AUTO_TEST_CASE(password_file_input_is_private_single_line_and_bounded) {
   auto directory = temporary_directory{};
   const auto path = directory.path / "password";
   {
      auto output = std::ofstream{path, std::ios::binary};
      output << "correct horse battery staple\n";
   }
   std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                std::filesystem::perm_options::replace);

   const auto request = keystore::password_request{
       .source = keystore::password_source::file,
       .file = path,
       .max_bytes = 64U,
   };
   BOOST_TEST(keystore::read_password(request).view() == "correct horse battery staple");

   std::filesystem::permissions(path,
                                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                    std::filesystem::perms::group_read,
                                std::filesystem::perm_options::replace);
   BOOST_CHECK_THROW(static_cast<void>(keystore::read_password(request)), keystore::exceptions::password_unavailable);

   std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                std::filesystem::perm_options::replace);
   {
      auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
      output << "first\nsecond\n";
   }
   BOOST_CHECK_THROW(static_cast<void>(keystore::read_password(request)), keystore::exceptions::password_unavailable);

   {
      auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
      output << "too-long";
   }
   auto bounded = request;
   bounded.max_bytes = 4U;
   BOOST_CHECK_THROW(static_cast<void>(keystore::read_password(bounded)), keystore::exceptions::size_limit_exceeded);
}

BOOST_AUTO_TEST_SUITE_END()
