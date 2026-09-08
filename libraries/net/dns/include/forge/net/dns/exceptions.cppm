module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

export module forge.net.dns.exceptions;

export import forge.exceptions;

export namespace forge::net::dns::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   closed = 2,
   canceled = 3,
   timeout = 4,
   not_found = 5,
   temporary_failure = 6,
   codec_error = 7,
   resource_limit = 8,
   internal = 9,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.net.dns")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using closed = forge::exceptions::coded_exception<code, code::closed>;
using canceled = forge::exceptions::coded_exception<code, code::canceled>;
using timeout = forge::exceptions::coded_exception<code, code::timeout>;
using not_found = forge::exceptions::coded_exception<code, code::not_found>;
using temporary_failure = forge::exceptions::coded_exception<code, code::temporary_failure>;
using codec_error = forge::exceptions::coded_exception<code, code::codec_error>;
using resource_limit = forge::exceptions::coded_exception<code, code::resource_limit>;
using internal = forge::exceptions::coded_exception<code, code::internal>;

[[nodiscard]] inline std::optional<code> code_of(const forge::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "forge.net.dns") {
      return std::nullopt;
   }
   return static_cast<code>(actual.value());
}

[[nodiscard]] inline bool is(const forge::exceptions::base& value, code expected) noexcept {
   return value.code() == forge::exceptions::make_error_code(expected);
}

} // namespace forge::net::dns::exceptions
