module;

#include <forge/exceptions/macros.hpp>

#include <utility>

module forge.auth.workload.subject_token;

import forge.auth.workload.exceptions;

namespace forge::auth::workload {

subject_token::subject_token(forge::crypto::core::secret_string secret) : secret_{std::move(secret)} {
   if (secret_.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::token_unavailable, "workload subject token must not be empty");
   }
}

subject_token::~subject_token() = default;

subject_token::subject_token(subject_token&&) noexcept = default;
subject_token& subject_token::operator=(subject_token&&) noexcept = default;

const forge::crypto::core::secret_string& subject_token::secret() const noexcept {
   return secret_;
}

} // namespace forge::auth::workload
