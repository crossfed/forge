module;

#include <forge/exceptions/macros.hpp>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

module forge.plugins.crypto.secrets.types;

import forge.crypto.symmetric.aes;
import forge.crypto.symmetric.kdf;
import forge.crypto.core.random;
import forge.crypto.core.types;
import forge.exceptions;
import forge.plugins.crypto.secrets.exceptions;

namespace forge::plugins::crypto::secrets {
namespace {

constexpr auto magic = std::array<std::uint8_t, 8>{'F', 'C', 'L', 'S', 'E', 'C', '1', 0};

[[noreturn]] void throw_invalid_encrypted_secret_file();

void append_u64(forge::crypto::core::bytes& out, std::uint64_t value) {
   for (auto i = 0U; i < 8U; ++i) {
      out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU));
   }
}

[[nodiscard]] std::uint64_t read_u64(const forge::crypto::core::bytes& input, std::size_t& offset) {
   if (input.size() - offset < 8U) {
      throw_invalid_encrypted_secret_file();
   }
   auto value = std::uint64_t{0};
   for (auto i = 0U; i < 8U; ++i) {
      value |= static_cast<std::uint64_t>(input[offset++]) << (i * 8U);
   }
   return value;
}

void append_bytes(forge::crypto::core::bytes& out, std::span<const std::uint8_t> value) {
   out.insert(out.end(), value.begin(), value.end());
}

[[nodiscard]] forge::crypto::core::bytes read_bytes(const forge::crypto::core::bytes& input, std::size_t& offset, std::uint64_t size) {
   if (size > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) || input.size() - offset < size) {
      throw_invalid_encrypted_secret_file();
   }
   auto output = forge::crypto::core::bytes{input.begin() + static_cast<std::ptrdiff_t>(offset),
                                    input.begin() + static_cast<std::ptrdiff_t>(offset + size)};
   offset += static_cast<std::size_t>(size);
   return output;
}

[[nodiscard]] forge::crypto::symmetric::aes::aes256_key derive_file_key(const std::string& passphrase,
                                                       const forge::crypto::core::bytes& salt,
                                                       std::uint64_t n,
                                                       std::uint64_t r,
                                                       std::uint64_t p,
                                                       std::uint64_t max_memory_bytes) {
   return forge::crypto::symmetric::aes::make_aes256_key(forge::crypto::symmetric::kdf::derive_scrypt({
      .password = passphrase,
      .salt = salt,
      .n = n,
      .r = r,
      .p = p,
      .max_memory_bytes = max_memory_bytes,
      .output_size = forge::crypto::symmetric::aes::aes256_key_size,
   }));
}

void validate_scrypt_limit(std::string_view name, std::uint64_t value, std::uint64_t max_value) {
   if (value == 0 || value > max_value) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "encrypted secret file scrypt parameter is outside configured limit",
                          forge::exceptions::ctx("parameter", name),
                          forge::exceptions::ctx("value", value),
                          forge::exceptions::ctx("max", max_value));
   }
}

void validate_scrypt_limits(std::uint64_t n,
                            std::uint64_t r,
                            std::uint64_t p,
                            std::uint64_t max_memory_bytes,
                            encrypted_file_decrypt_limits limits) {
   validate_scrypt_limit("n", n, limits.max_scrypt_n);
   validate_scrypt_limit("r", r, limits.max_scrypt_r);
   validate_scrypt_limit("p", p, limits.max_scrypt_p);
   validate_scrypt_limit("max_memory_bytes", max_memory_bytes, limits.max_scrypt_memory_bytes);
}

[[noreturn]] void throw_invalid_encrypted_secret_file() {
   FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "encrypted secret file cannot be decrypted");
}

[[noreturn]] void throw_invalid_encrypted_secret_file_parameter(
   const forge::exceptions::runtime_coded_exception<forge::crypto::symmetric::aes::exceptions::code>& error) {
   switch (error.value()) {
   case forge::crypto::symmetric::aes::exceptions::code::invalid_nonce:
   case forge::crypto::symmetric::aes::exceptions::code::invalid_tag:
   case forge::crypto::symmetric::aes::exceptions::code::authentication_failed:
      throw_invalid_encrypted_secret_file();
   default:
      throw;
   }
}

[[noreturn]] void throw_invalid_encrypted_secret_file_parameter(
   const forge::exceptions::runtime_coded_exception<forge::crypto::symmetric::kdf::exceptions::code>& error) {
   switch (error.value()) {
   case forge::crypto::symmetric::kdf::exceptions::code::invalid_options:
   case forge::crypto::symmetric::kdf::exceptions::code::backend_error:
      throw_invalid_encrypted_secret_file();
   default:
      throw;
   }
}

[[nodiscard]] forge::crypto::core::bytes make_header(std::uint64_t n,
                                             std::uint64_t r,
                                             std::uint64_t p,
                                             std::uint64_t max_memory_bytes,
                                             const forge::crypto::core::bytes& salt,
                                             const forge::crypto::core::bytes& nonce,
                                             std::uint64_t ciphertext_size) {
   auto header = forge::crypto::core::bytes{};
   append_bytes(header, magic);
   append_u64(header, n);
   append_u64(header, r);
   append_u64(header, p);
   append_u64(header, max_memory_bytes);
   append_u64(header, salt.size());
   append_u64(header, nonce.size());
   append_u64(header, ciphertext_size);
   append_bytes(header, salt);
   append_bytes(header, nonce);
   return header;
}

} // namespace

forge::crypto::core::bytes encrypt_secret_file(encrypted_file_encrypt_request request) {
   if (request.passphrase.empty() || request.plaintext.empty()) {
      FORGE_THROW("encrypted secret file requires passphrase and plaintext");
   }
   if (request.salt.empty()) {
      request.salt = forge::crypto::core::random_bytes(16);
   }
   if (request.nonce.empty()) {
      request.nonce = forge::crypto::core::random_bytes(forge::crypto::symmetric::aes::aes_gcm_nonce_size);
   }

   const auto header = make_header(request.scrypt_n,
                                   request.scrypt_r,
                                   request.scrypt_p,
                                   request.scrypt_max_memory_bytes,
                                   request.salt,
                                   request.nonce,
                                   request.plaintext.size());
   auto encrypted = forge::crypto::symmetric::aes::encrypt_aes256_gcm({
      .key = derive_file_key(request.passphrase,
                             request.salt,
                             request.scrypt_n,
                             request.scrypt_r,
                             request.scrypt_p,
                             request.scrypt_max_memory_bytes),
      .nonce = request.nonce,
      .plaintext = std::move(request.plaintext),
      .aad = header,
   });

   auto output = header;
   append_bytes(output, encrypted.tag);
   append_bytes(output, encrypted.ciphertext);
   return output;
}

forge::crypto::core::bytes decrypt_secret_file(const forge::crypto::core::bytes& container,
                                       const std::string& passphrase,
                                       encrypted_file_decrypt_limits limits) {
   if (container.size() < magic.size() || !std::equal(magic.begin(), magic.end(), container.begin())) {
      throw_invalid_encrypted_secret_file();
   }
   auto offset = magic.size();
   const auto n = read_u64(container, offset);
   const auto r = read_u64(container, offset);
   const auto p = read_u64(container, offset);
   const auto max_memory_bytes = read_u64(container, offset);
   const auto salt_size = read_u64(container, offset);
   const auto nonce_size = read_u64(container, offset);
   const auto ciphertext_size = read_u64(container, offset);
   validate_scrypt_limits(n, r, p, max_memory_bytes, limits);
   if (ciphertext_size > limits.max_plaintext_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::size_limit_exceeded, "encrypted secret file plaintext exceeds configured limit",
                          forge::exceptions::ctx("size", ciphertext_size),
                          forge::exceptions::ctx("max", limits.max_plaintext_bytes));
   }
   auto salt = read_bytes(container, offset, salt_size);
   auto nonce = read_bytes(container, offset, nonce_size);
   auto tag = read_bytes(container, offset, forge::crypto::symmetric::aes::aes_gcm_tag_size);
   auto ciphertext = read_bytes(container, offset, ciphertext_size);
   if (offset != container.size()) {
      throw_invalid_encrypted_secret_file();
   }
   auto header = make_header(n, r, p, max_memory_bytes, salt, nonce, ciphertext_size);
   try {
      return forge::crypto::symmetric::aes::decrypt_aes256_gcm({
         .key = derive_file_key(passphrase, salt, n, r, p, max_memory_bytes),
         .encrypted =
            forge::crypto::symmetric::aes::aes256_gcm_ciphertext{
               .nonce = std::move(nonce),
               .tag = std::move(tag),
               .ciphertext = std::move(ciphertext),
            },
         .aad = std::move(header),
      });
   } catch (const forge::crypto::symmetric::aes::exceptions::invalid_nonce&) {
      throw_invalid_encrypted_secret_file();
   } catch (const forge::crypto::symmetric::aes::exceptions::invalid_tag&) {
      throw_invalid_encrypted_secret_file();
   } catch (const forge::crypto::symmetric::aes::exceptions::authentication_failed&) {
      throw_invalid_encrypted_secret_file();
   } catch (const forge::crypto::symmetric::kdf::exceptions::invalid_options&) {
      throw_invalid_encrypted_secret_file();
   } catch (const forge::crypto::symmetric::kdf::exceptions::backend_error&) {
      throw_invalid_encrypted_secret_file();
   } catch (const forge::exceptions::runtime_coded_exception<forge::crypto::symmetric::aes::exceptions::code>& error) {
      throw_invalid_encrypted_secret_file_parameter(error);
   } catch (const forge::exceptions::runtime_coded_exception<forge::crypto::symmetric::kdf::exceptions::code>& error) {
      throw_invalid_encrypted_secret_file_parameter(error);
   }
}

} // namespace forge::plugins::crypto::secrets
