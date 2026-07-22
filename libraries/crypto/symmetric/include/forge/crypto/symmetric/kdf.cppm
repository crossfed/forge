module;

#include <forge/exceptions/macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

export module forge.crypto.symmetric.kdf;

export import forge.exceptions;
import forge.crypto.core.types;

export namespace forge::crypto::symmetric::kdf::exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   invalid_options = 2,
   backend_error = 3,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.symmetric.kdf")

using invalid_key = forge::exceptions::coded_exception<code, code::invalid_key>;
using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace forge::crypto::symmetric::kdf::exceptions

export namespace forge::crypto::symmetric::kdf {

inline constexpr auto default_derived_key_size = std::size_t{32};

struct hkdf_sha256_request {
   core::bytes secret;
   core::bytes salt;
   core::bytes info;
   std::size_t output_size = default_derived_key_size;
};

struct hkdf_sha256_span_request {
   std::span<const std::uint8_t> secret;
   std::span<const std::uint8_t> salt;
   std::span<const std::uint8_t> info;
   std::size_t output_size = default_derived_key_size;
};

struct scrypt_request {
   std::string password;
   core::bytes salt;
   std::uint64_t n = 16'384;
   std::uint64_t r = 8;
   std::uint64_t p = 1;
   std::uint64_t max_memory_bytes = 32ULL * 1024ULL * 1024ULL;
   std::size_t output_size = default_derived_key_size;
};

[[nodiscard]] core::bytes derive_hkdf_sha256(const hkdf_sha256_request& request);
[[nodiscard]] core::bytes derive_hkdf_sha256(const hkdf_sha256_span_request& request);
[[nodiscard]] core::bytes derive_scrypt(const scrypt_request& request);

} // namespace forge::crypto
