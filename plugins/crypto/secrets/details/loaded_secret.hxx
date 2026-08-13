#pragma once

namespace forge::plugins::crypto::secrets {

struct loaded_secret {
   std::string id;
   secret_kind kind = secret_kind::symmetric_key;
   forge::crypto::core::secret_bytes material;
   std::vector<std::string> purposes;
   std::vector<operation> operations;
   bool allow_raw_export = false;
   std::uint64_t max_plaintext_bytes = default_max_plaintext_bytes;
   std::uint64_t max_ciphertext_bytes = default_max_ciphertext_bytes;
   std::uint64_t max_aad_bytes = default_max_aad_bytes;
};

} // namespace forge::plugins::crypto::secrets
