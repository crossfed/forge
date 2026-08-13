#pragma once

#include "loaded_secret.hxx"

namespace forge::plugins::crypto::secrets {

struct plugin::impl {
   [[nodiscard]] snapshot status(query value) const;
   [[nodiscard]] get_result get_bytes(get_request value) const;
   [[nodiscard]] derive_result derive_hkdf_sha256(derive_request value) const;
   [[nodiscard]] aead_encrypt_result encrypt_aes_gcm(aead_encrypt_request value) const;
   [[nodiscard]] aead_decrypt_result decrypt_aes_gcm(aead_decrypt_request value) const;

   std::map<std::string, loaded_secret> secrets;
   bool stopping = false;
};

} // namespace forge::plugins::crypto::secrets
