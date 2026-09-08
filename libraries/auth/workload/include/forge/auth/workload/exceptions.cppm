module;

#include <cstdint>
#include <optional>
#include <string_view>

#include <forge/exceptions/macros.hpp>

export module forge.auth.workload.exceptions;

export import forge.exceptions;

export namespace forge::auth::workload::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   insecure_source = 2,
   source_too_large = 3,
   source_rotated = 4,
   io_failure = 5,
   token_unavailable = 6,
   unavailable = 7,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.auth.workload")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using insecure_source = forge::exceptions::coded_exception<code, code::insecure_source>;
using source_too_large = forge::exceptions::coded_exception<code, code::source_too_large>;
using source_rotated = forge::exceptions::coded_exception<code, code::source_rotated>;
using io_failure = forge::exceptions::coded_exception<code, code::io_failure>;
using token_unavailable = forge::exceptions::coded_exception<code, code::token_unavailable>;
using unavailable = forge::exceptions::coded_exception<code, code::unavailable>;

[[nodiscard]] inline std::optional<code> code_of(const forge::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "forge.auth.workload") {
      return std::nullopt;
   }
   switch (actual.value()) {
   case static_cast<int>(code::invalid_options):
      return code::invalid_options;
   case static_cast<int>(code::insecure_source):
      return code::insecure_source;
   case static_cast<int>(code::source_too_large):
      return code::source_too_large;
   case static_cast<int>(code::source_rotated):
      return code::source_rotated;
   case static_cast<int>(code::io_failure):
      return code::io_failure;
   case static_cast<int>(code::token_unavailable):
      return code::token_unavailable;
   case static_cast<int>(code::unavailable):
      return code::unavailable;
   default:
      return std::nullopt;
   }
}

[[nodiscard]] inline bool is(const forge::exceptions::base& value, code expected) noexcept {
   return value.code() == forge::exceptions::make_error_code(expected);
}

} // namespace forge::auth::workload::exceptions
