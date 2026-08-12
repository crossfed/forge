module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

module forge.crypto.keystore.encrypted_file;

import forge.crypto.core.random;
import forge.crypto.symmetric.aes;
import forge.crypto.symmetric.kdf;

namespace forge::crypto::keystore {
namespace {

constexpr auto magic = std::array<std::uint8_t, 8>{'F', 'O', 'R', 'G', 'E', 'K', 'S', 1};
constexpr auto salt_size = std::size_t{16};

struct bytes_wiper {
   core::bytes* value;

   ~bytes_wiper() {
      core::secure_erase(*value);
   }
};

[[noreturn]] void throw_invalid_file() {
   FORGE_THROW_EXCEPTION(exceptions::invalid_file, "encrypted key file cannot be decrypted");
}

void append_u64(core::bytes& output, std::uint64_t value) {
   for (auto index = 0U; index < 8U; ++index) {
      output.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU));
   }
}

void append_bytes(core::bytes& output, std::span<const std::uint8_t> value) {
   output.insert(output.end(), value.begin(), value.end());
}

std::uint64_t read_u64(const core::bytes& input, std::size_t& offset) {
   if (offset > input.size() || input.size() - offset < 8U) {
      throw_invalid_file();
   }
   auto value = std::uint64_t{};
   for (auto index = 0U; index < 8U; ++index) {
      value |= static_cast<std::uint64_t>(input[offset++]) << (index * 8U);
   }
   return value;
}

core::bytes read_bytes(const core::bytes& input, std::size_t& offset, std::uint64_t size) {
   if (size > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) || offset > input.size() ||
       input.size() - offset < size) {
      throw_invalid_file();
   }
   auto result = core::bytes{input.begin() + static_cast<std::ptrdiff_t>(offset),
                             input.begin() + static_cast<std::ptrdiff_t>(offset + size)};
   offset += static_cast<std::size_t>(size);
   return result;
}

void require_limit(std::string_view name, std::uint64_t value, std::uint64_t maximum) {
   if (value == 0U || value > maximum) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_file, "encrypted key file KDF parameter exceeds its limit",
                            forge::exceptions::ctx("parameter", name), forge::exceptions::ctx("value", value),
                            forge::exceptions::ctx("maximum", maximum));
   }
}

void validate(encryption_options options) {
   if (options.scrypt_n < 2U || (options.scrypt_n & (options.scrypt_n - 1U)) != 0U || options.scrypt_r == 0U ||
       options.scrypt_p == 0U || options.scrypt_max_memory_bytes == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid scrypt options");
   }
}

symmetric::aes::aes256_key derive_key(const core::secret_string& password, const core::bytes& salt,
                                      encryption_options options) {
   const auto password_view = password.view();
   auto derived = symmetric::kdf::derive_scrypt(symmetric::kdf::scrypt_span_request{
       .password = std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(password_view.data()),
                                                 password_view.size()},
       .salt = salt,
       .n = options.scrypt_n,
       .r = options.scrypt_r,
       .p = options.scrypt_p,
       .max_memory_bytes = options.scrypt_max_memory_bytes,
       .output_size = symmetric::aes::aes256_key_size,
   });
   auto wipe_derived = bytes_wiper{&derived};
   return symmetric::aes::make_aes256_key(derived);
}

core::bytes make_header(encryption_options options, const core::bytes& salt, const core::bytes& nonce,
                        std::uint64_t ciphertext_size) {
   auto result = core::bytes{};
   append_bytes(result, magic);
   append_u64(result, options.scrypt_n);
   append_u64(result, options.scrypt_r);
   append_u64(result, options.scrypt_p);
   append_u64(result, options.scrypt_max_memory_bytes);
   append_u64(result, salt.size());
   append_u64(result, nonce.size());
   append_u64(result, ciphertext_size);
   append_bytes(result, salt);
   append_bytes(result, nonce);
   return result;
}

} // namespace

core::bytes encrypt_file(encrypted_file_request request) {
   validate(request.encryption);
   if (request.password.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "encrypted key file requires a password");
   }
   const auto salt = core::random_bytes(salt_size);
   const auto nonce = core::random_bytes(symmetric::aes::aes_gcm_nonce_size);

   const auto header = make_header(request.encryption, salt, nonce, request.plaintext.size());
   auto encryption_request = symmetric::aes::aes256_gcm_encrypt_request{
       .key = derive_key(request.password, salt, request.encryption),
       .nonce = nonce,
       .plaintext = request.plaintext.copy(),
       .aad = header,
   };
   auto wipe_plaintext = bytes_wiper{&encryption_request.plaintext};
   auto encrypted = symmetric::aes::encrypt_aes256_gcm(encryption_request);
   auto result = header;
   append_bytes(result, encrypted.tag);
   append_bytes(result, encrypted.ciphertext);
   return result;
}

core::secret_bytes decrypt_file(const core::bytes& container, const core::secret_string& password,
                                decrypt_limits limits) try {
   if (password.empty() || container.size() < magic.size() ||
       !std::equal(magic.begin(), magic.end(), container.begin())) {
      throw_invalid_file();
   }
   auto offset = magic.size();
   auto encryption = encryption_options{
       .scrypt_n = read_u64(container, offset),
       .scrypt_r = read_u64(container, offset),
       .scrypt_p = read_u64(container, offset),
       .scrypt_max_memory_bytes = read_u64(container, offset),
   };
   require_limit("n", encryption.scrypt_n, limits.max_scrypt_n);
   require_limit("r", encryption.scrypt_r, limits.max_scrypt_r);
   require_limit("p", encryption.scrypt_p, limits.max_scrypt_p);
   require_limit("memory", encryption.scrypt_max_memory_bytes, limits.max_scrypt_memory_bytes);
   validate(encryption);
   const auto encoded_salt_size = read_u64(container, offset);
   const auto encoded_nonce_size = read_u64(container, offset);
   const auto ciphertext_size = read_u64(container, offset);
   if (ciphertext_size > limits.max_plaintext_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::size_limit_exceeded, "encrypted key file exceeds its plaintext limit",
                            forge::exceptions::ctx("size", ciphertext_size),
                            forge::exceptions::ctx("maximum", limits.max_plaintext_bytes));
   }
   constexpr auto fixed_body_size = salt_size + symmetric::aes::aes_gcm_nonce_size +
                                    symmetric::aes::aes_gcm_tag_size;
   if (encoded_salt_size != salt_size || encoded_nonce_size != symmetric::aes::aes_gcm_nonce_size ||
       ciphertext_size > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)() - fixed_body_size) ||
       offset > container.size() || container.size() - offset != fixed_body_size + ciphertext_size) {
      throw_invalid_file();
   }
   auto salt = read_bytes(container, offset, encoded_salt_size);
   auto nonce = read_bytes(container, offset, encoded_nonce_size);
   auto tag = read_bytes(container, offset, symmetric::aes::aes_gcm_tag_size);
   auto ciphertext = read_bytes(container, offset, ciphertext_size);
   if (offset != container.size()) {
      throw_invalid_file();
   }
   auto header = make_header(encryption, salt, nonce, ciphertext_size);
   auto decryption_request = symmetric::aes::aes256_gcm_decrypt_request{
       .key = derive_key(password, salt, encryption),
       .encrypted =
           symmetric::aes::aes256_gcm_ciphertext{
               .nonce = std::move(nonce),
               .tag = std::move(tag),
               .ciphertext = std::move(ciphertext),
           },
       .aad = std::move(header),
   };
   return core::secret_bytes{symmetric::aes::decrypt_aes256_gcm(decryption_request)};
} catch (const exceptions::size_limit_exceeded&) {
   throw;
} catch (const forge::exceptions::base&) {
   throw_invalid_file();
}

} // namespace forge::crypto::keystore
