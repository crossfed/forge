module;

#include <cstdint>
#include <optional>
#include <string_view>

#include <forge/exceptions/macros.hpp>

export module forge.auth.pairing.exceptions;

export import forge.exceptions;

export namespace forge::auth::pairing::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   token_invalid = 2,
   replayed = 3,
   expired = 4,
   capacity_exceeded = 5,
   invalid_state = 6,
   scope_invalid = 7,
   identity_invalid = 8,
   generation_exhausted = 9,
   credential_id_invalid = 10,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.auth.pairing")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using token_invalid = forge::exceptions::coded_exception<code, code::token_invalid>;
using replayed = forge::exceptions::coded_exception<code, code::replayed>;
using expired = forge::exceptions::coded_exception<code, code::expired>;
using capacity_exceeded = forge::exceptions::coded_exception<code, code::capacity_exceeded>;
using invalid_state = forge::exceptions::coded_exception<code, code::invalid_state>;
using scope_invalid = forge::exceptions::coded_exception<code, code::scope_invalid>;
using identity_invalid = forge::exceptions::coded_exception<code, code::identity_invalid>;
using generation_exhausted = forge::exceptions::coded_exception<code, code::generation_exhausted>;
using credential_id_invalid = forge::exceptions::coded_exception<code, code::credential_id_invalid>;

[[nodiscard]] inline std::optional<code> code_of(const forge::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "forge.auth.pairing") {
      return std::nullopt;
   }
   switch (actual.value()) {
   case static_cast<int>(code::invalid_options):
      return code::invalid_options;
   case static_cast<int>(code::token_invalid):
      return code::token_invalid;
   case static_cast<int>(code::replayed):
      return code::replayed;
   case static_cast<int>(code::expired):
      return code::expired;
   case static_cast<int>(code::capacity_exceeded):
      return code::capacity_exceeded;
   case static_cast<int>(code::invalid_state):
      return code::invalid_state;
   case static_cast<int>(code::scope_invalid):
      return code::scope_invalid;
   case static_cast<int>(code::identity_invalid):
      return code::identity_invalid;
   case static_cast<int>(code::generation_exhausted):
      return code::generation_exhausted;
   case static_cast<int>(code::credential_id_invalid):
      return code::credential_id_invalid;
   default:
      return std::nullopt;
   }
}

[[nodiscard]] inline bool is(const forge::exceptions::base& value, code expected) noexcept {
   return value.code() == forge::exceptions::make_error_code(expected);
}

} // namespace forge::auth::pairing::exceptions
