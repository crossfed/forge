module;

#include <cstdint>
#include <optional>
#include <string_view>

#include <forge/exceptions/macros.hpp>

export module forge.auth.http.exceptions;

export import forge.exceptions;

export namespace forge::auth::http::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   malformed_evidence = 2,
   duplicate_evidence = 3,
   missing_evidence = 4,
   origin_mismatch = 5,
   method_not_allowed = 6,
   csrf_mismatch = 7,
   scope_denied = 8,
   internal_failure = 9,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.auth.http")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using malformed_evidence = forge::exceptions::coded_exception<code, code::malformed_evidence>;
using duplicate_evidence = forge::exceptions::coded_exception<code, code::duplicate_evidence>;
using missing_evidence = forge::exceptions::coded_exception<code, code::missing_evidence>;
using origin_mismatch = forge::exceptions::coded_exception<code, code::origin_mismatch>;
using method_not_allowed = forge::exceptions::coded_exception<code, code::method_not_allowed>;
using csrf_mismatch = forge::exceptions::coded_exception<code, code::csrf_mismatch>;
using scope_denied = forge::exceptions::coded_exception<code, code::scope_denied>;
using internal_failure = forge::exceptions::coded_exception<code, code::internal_failure>;

[[nodiscard]] inline std::optional<code> code_of(const forge::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "forge.auth.http") {
      return std::nullopt;
   }
   switch (actual.value()) {
   case static_cast<int>(code::invalid_options):
      return code::invalid_options;
   case static_cast<int>(code::malformed_evidence):
      return code::malformed_evidence;
   case static_cast<int>(code::duplicate_evidence):
      return code::duplicate_evidence;
   case static_cast<int>(code::missing_evidence):
      return code::missing_evidence;
   case static_cast<int>(code::origin_mismatch):
      return code::origin_mismatch;
   case static_cast<int>(code::method_not_allowed):
      return code::method_not_allowed;
   case static_cast<int>(code::csrf_mismatch):
      return code::csrf_mismatch;
   case static_cast<int>(code::scope_denied):
      return code::scope_denied;
   case static_cast<int>(code::internal_failure):
      return code::internal_failure;
   default:
      return std::nullopt;
   }
}

[[nodiscard]] inline bool is(const forge::exceptions::base& value, code expected) noexcept {
   return value.code() == forge::exceptions::make_error_code(expected);
}

} // namespace forge::auth::http::exceptions
