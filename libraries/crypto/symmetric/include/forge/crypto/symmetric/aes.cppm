module;

#include <forge/exceptions/macros.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

export module forge.crypto.symmetric.aes;

export import forge.exceptions;
import forge.crypto.core.types;

export namespace forge::crypto::symmetric::aes::exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   invalid_nonce = 2,
   invalid_tag = 3,
   invalid_options = 4,
   authentication_failed = 5,
   backend_error = 6,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.symmetric.aes")

using invalid_key = forge::exceptions::coded_exception<code, code::invalid_key>;
using invalid_nonce = forge::exceptions::coded_exception<code, code::invalid_nonce>;
using invalid_tag = forge::exceptions::coded_exception<code, code::invalid_tag>;
using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using authentication_failed = forge::exceptions::coded_exception<code, code::authentication_failed>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace forge::crypto::symmetric::aes::exceptions

export namespace forge::crypto::symmetric::aes {

inline constexpr auto aes256_key_size = std::size_t{32};
inline constexpr auto aes_cbc_iv_size = std::size_t{16};
inline constexpr auto aes_gcm_nonce_size = std::size_t{12};
inline constexpr auto aes_gcm_tag_size = std::size_t{16};

using aes_byte_sink = std::function<void(std::span<const std::uint8_t>)>;

struct aes256_key {
   aes256_key() = default;
   ~aes256_key();

   aes256_key(const aes256_key&) = default;
   aes256_key& operator=(const aes256_key&) = default;
   aes256_key(aes256_key&& other) noexcept;
   aes256_key& operator=(aes256_key&& other) noexcept;

   std::array<std::uint8_t, aes256_key_size> bytes{};
};

struct aes256_gcm_authentication {
   core::bytes nonce;
   core::bytes tag;
};

struct aes256_gcm_ciphertext {
   core::bytes nonce;
   core::bytes tag;
   core::bytes ciphertext;
};

struct aes256_gcm_encrypt_request {
   aes256_key key;
   core::bytes nonce;
   core::bytes plaintext;
   core::bytes aad;
};

struct aes256_gcm_decrypt_request {
   aes256_key key;
   aes256_gcm_ciphertext encrypted;
   core::bytes aad;
};

struct aes256_gcm_encoder_options {
   aes256_key key;
   core::bytes nonce;
   core::bytes aad;
   aes_byte_sink ciphertext_sink;
};

struct aes256_gcm_decoder_options {
   aes256_key key;
   core::bytes nonce;
   core::bytes tag;
   core::bytes aad;
   aes_byte_sink plaintext_sink;
};

struct aes256_cbc_ciphertext {
   core::bytes iv;
   core::bytes ciphertext;
};

struct aes256_cbc_encrypt_request {
   aes256_key key;
   core::bytes iv;
   core::bytes plaintext;
};

struct aes256_cbc_decrypt_request {
   aes256_key key;
   aes256_cbc_ciphertext encrypted;
};

[[nodiscard]] aes256_key make_aes256_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] aes256_key generate_aes256_key();

class aes256_gcm_encoder {
 public:
   explicit aes256_gcm_encoder(aes256_gcm_encoder_options options);
   ~aes256_gcm_encoder();

   aes256_gcm_encoder(aes256_gcm_encoder&&) noexcept;
   aes256_gcm_encoder& operator=(aes256_gcm_encoder&&) noexcept;

   aes256_gcm_encoder(const aes256_gcm_encoder&) = delete;
   aes256_gcm_encoder& operator=(const aes256_gcm_encoder&) = delete;

   void write(const char* data, std::size_t size);
   void write(std::span<const std::uint8_t> data);

   [[nodiscard]] aes256_gcm_authentication finalize();

 private:
   struct impl;
   static std::unique_ptr<impl> make_impl(aes256_gcm_encoder_options& options);
   std::unique_ptr<impl> _impl;
};

class aes256_gcm_decoder {
 public:
   explicit aes256_gcm_decoder(aes256_gcm_decoder_options options);
   ~aes256_gcm_decoder();

   aes256_gcm_decoder(aes256_gcm_decoder&&) noexcept;
   aes256_gcm_decoder& operator=(aes256_gcm_decoder&&) noexcept;

   aes256_gcm_decoder(const aes256_gcm_decoder&) = delete;
   aes256_gcm_decoder& operator=(const aes256_gcm_decoder&) = delete;

   void write(const char* data, std::size_t size);
   void write(std::span<const std::uint8_t> data);

   void finalize();

 private:
   struct impl;
   static std::unique_ptr<impl> make_impl(aes256_gcm_decoder_options& options);
   std::unique_ptr<impl> _impl;
};

[[nodiscard]] aes256_gcm_ciphertext encrypt_aes256_gcm(const aes256_gcm_encrypt_request& request);

[[nodiscard]] core::bytes decrypt_aes256_gcm(const aes256_gcm_decrypt_request& request);

[[nodiscard]] aes256_cbc_ciphertext encrypt_aes256_cbc(const aes256_cbc_encrypt_request& request);

[[nodiscard]] core::bytes decrypt_aes256_cbc(const aes256_cbc_decrypt_request& request);

} // namespace forge::crypto::symmetric::aes
