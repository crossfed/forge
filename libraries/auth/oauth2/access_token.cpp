module;

#include <forge/exceptions/macros.hpp>

#include <utility>

module forge.auth.oauth2.access_token;

import forge.auth.oauth2.exceptions;

namespace forge::auth::oauth2 {

access_token::access_token(forge::crypto::core::secret_string secret, access_token_metadata metadata)
    : secret_{std::move(secret)}, metadata_{std::move(metadata)} {
   if (secret_.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::token_unavailable, "access token must not be empty");
   }
}

access_token::~access_token() = default;

access_token::access_token(access_token&&) noexcept = default;
access_token& access_token::operator=(access_token&&) noexcept = default;

const forge::crypto::core::secret_string& access_token::secret() const noexcept {
   return secret_;
}

const access_token_metadata& access_token::metadata() const noexcept {
   return metadata_;
}

} // namespace forge::auth::oauth2
