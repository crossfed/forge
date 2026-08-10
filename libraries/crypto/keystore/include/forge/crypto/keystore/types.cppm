module;

#include <cstddef>
#include <cstdint>
#include <filesystem>

export module forge.crypto.keystore.types;

export import forge.crypto.core.secret_bytes;
export import forge.crypto.core.secret_string;
export import forge.crypto.core.types;
export import forge.crypto.signer.types;

export namespace forge::crypto::keystore {

inline constexpr auto default_scrypt_n = std::uint64_t{16'384};
inline constexpr auto default_scrypt_r = std::uint64_t{8};
inline constexpr auto default_scrypt_p = std::uint64_t{1};
inline constexpr auto default_scrypt_memory_bytes = std::uint64_t{32} * 1024U * 1024U;

struct encryption_options {
   std::uint64_t scrypt_n = default_scrypt_n;
   std::uint64_t scrypt_r = default_scrypt_r;
   std::uint64_t scrypt_p = default_scrypt_p;
   std::uint64_t scrypt_max_memory_bytes = default_scrypt_memory_bytes;

   bool operator==(const encryption_options&) const = default;
};

struct decrypt_limits {
   std::uint64_t max_plaintext_bytes = 16U * 1024U * 1024U;
   std::uint64_t max_scrypt_n = 1U << 20U;
   std::uint64_t max_scrypt_r = 32U;
   std::uint64_t max_scrypt_p = 16U;
   std::uint64_t max_scrypt_memory_bytes = 256U * 1024U * 1024U;

   bool operator==(const decrypt_limits&) const = default;
};

struct encrypted_file_request {
   core::secret_bytes plaintext;
   core::secret_string password;
   encryption_options encryption;
};

struct store_options {
   encryption_options encryption;
   decrypt_limits limits;
   std::size_t max_keys = 1'024;
   std::size_t max_key_id_bytes = 256;

   bool operator==(const store_options&) const = default;
};

} // namespace forge::crypto::keystore
