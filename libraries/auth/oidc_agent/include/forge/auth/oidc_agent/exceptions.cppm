module;

#include <cstdint>
#include <optional>
#include <string_view>

#include <forge/exceptions/macros.hpp>

export module forge.auth.oidc_agent.exceptions;

export import forge.exceptions;

export namespace forge::auth::oidc_agent::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   invalid_response = 2,
   unavailable = 3,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.auth.oidc_agent")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using invalid_response = forge::exceptions::coded_exception<code, code::invalid_response>;
using unavailable = forge::exceptions::coded_exception<code, code::unavailable>;

[[nodiscard]] inline std::optional<code> code_of(const forge::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "forge.auth.oidc_agent") {
      return std::nullopt;
   }
   switch (actual.value()) {
   case static_cast<int>(code::invalid_options):
      return code::invalid_options;
   case static_cast<int>(code::invalid_response):
      return code::invalid_response;
   case static_cast<int>(code::unavailable):
      return code::unavailable;
   default:
      return std::nullopt;
   }
}

[[nodiscard]] inline bool is(const forge::exceptions::base& value, code expected) noexcept {
   return value.code() == forge::exceptions::make_error_code(expected);
}

} // namespace forge::auth::oidc_agent::exceptions
