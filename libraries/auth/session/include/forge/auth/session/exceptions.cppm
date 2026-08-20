module;

#include <cstdint>
#include <optional>
#include <string_view>

#include <forge/exceptions/macros.hpp>

export module forge.auth.session.exceptions;

export import forge.exceptions;

export namespace forge::auth::session::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   token_invalid = 2,
   csrf_invalid = 3,
   expired = 4,
   idle_expired = 5,
   invalid_state = 6,
   replayed = 7,
   credential_mismatch = 8,
   credential_revoked = 9,
   identity_mismatch = 10,
   scope_mismatch = 11,
   secret_collision = 12,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.auth.session")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using token_invalid = forge::exceptions::coded_exception<code, code::token_invalid>;
using csrf_invalid = forge::exceptions::coded_exception<code, code::csrf_invalid>;
using expired = forge::exceptions::coded_exception<code, code::expired>;
using idle_expired = forge::exceptions::coded_exception<code, code::idle_expired>;
using invalid_state = forge::exceptions::coded_exception<code, code::invalid_state>;
using replayed = forge::exceptions::coded_exception<code, code::replayed>;
using credential_mismatch = forge::exceptions::coded_exception<code, code::credential_mismatch>;
using credential_revoked = forge::exceptions::coded_exception<code, code::credential_revoked>;
using identity_mismatch = forge::exceptions::coded_exception<code, code::identity_mismatch>;
using scope_mismatch = forge::exceptions::coded_exception<code, code::scope_mismatch>;
using secret_collision = forge::exceptions::coded_exception<code, code::secret_collision>;

[[nodiscard]] inline std::optional<code> code_of(const forge::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "forge.auth.session") {
      return std::nullopt;
   }
   switch (actual.value()) {
   case static_cast<int>(code::invalid_options):
      return code::invalid_options;
   case static_cast<int>(code::token_invalid):
      return code::token_invalid;
   case static_cast<int>(code::csrf_invalid):
      return code::csrf_invalid;
   case static_cast<int>(code::expired):
      return code::expired;
   case static_cast<int>(code::idle_expired):
      return code::idle_expired;
   case static_cast<int>(code::invalid_state):
      return code::invalid_state;
   case static_cast<int>(code::replayed):
      return code::replayed;
   case static_cast<int>(code::credential_mismatch):
      return code::credential_mismatch;
   case static_cast<int>(code::credential_revoked):
      return code::credential_revoked;
   case static_cast<int>(code::identity_mismatch):
      return code::identity_mismatch;
   case static_cast<int>(code::scope_mismatch):
      return code::scope_mismatch;
   case static_cast<int>(code::secret_collision):
      return code::secret_collision;
   default:
      return std::nullopt;
   }
}

[[nodiscard]] inline bool is(const forge::exceptions::base& value, code expected) noexcept {
   return value.code() == forge::exceptions::make_error_code(expected);
}

} // namespace forge::auth::session::exceptions
