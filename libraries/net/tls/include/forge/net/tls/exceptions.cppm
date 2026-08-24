module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <forge/exceptions/macros.hpp>

export module forge.net.tls.exceptions;

export import forge.exceptions;

export namespace forge::net::tls::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   identity_invalid = 2,
   trust_anchors_invalid = 3,
   verification_configuration_invalid = 4,
   context_creation_failed = 5,
   peer_certificate_missing = 6,
   peer_certificate_invalid = 7,
   certificate_chain_untrusted = 8,
   hostname_mismatch = 9,
   fingerprint_mismatch = 10,
   peer_rejected = 11,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.net.tls")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using identity_invalid = forge::exceptions::coded_exception<code, code::identity_invalid>;
using trust_anchors_invalid = forge::exceptions::coded_exception<code, code::trust_anchors_invalid>;
using verification_configuration_invalid =
    forge::exceptions::coded_exception<code, code::verification_configuration_invalid>;
using context_creation_failed = forge::exceptions::coded_exception<code, code::context_creation_failed>;
using peer_certificate_missing = forge::exceptions::coded_exception<code, code::peer_certificate_missing>;
using peer_certificate_invalid = forge::exceptions::coded_exception<code, code::peer_certificate_invalid>;
using certificate_chain_untrusted = forge::exceptions::coded_exception<code, code::certificate_chain_untrusted>;
using hostname_mismatch = forge::exceptions::coded_exception<code, code::hostname_mismatch>;
using fingerprint_mismatch = forge::exceptions::coded_exception<code, code::fingerprint_mismatch>;
using peer_rejected = forge::exceptions::coded_exception<code, code::peer_rejected>;

[[nodiscard]] inline std::optional<code> code_of(const forge::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "forge.net.tls") {
      return std::nullopt;
   }
   if (actual == forge::exceptions::make_error_code(code::invalid_options)) {
      return code::invalid_options;
   }
   if (actual == forge::exceptions::make_error_code(code::identity_invalid)) {
      return code::identity_invalid;
   }
   if (actual == forge::exceptions::make_error_code(code::trust_anchors_invalid)) {
      return code::trust_anchors_invalid;
   }
   if (actual == forge::exceptions::make_error_code(code::verification_configuration_invalid)) {
      return code::verification_configuration_invalid;
   }
   if (actual == forge::exceptions::make_error_code(code::context_creation_failed)) {
      return code::context_creation_failed;
   }
   if (actual == forge::exceptions::make_error_code(code::peer_certificate_missing)) {
      return code::peer_certificate_missing;
   }
   if (actual == forge::exceptions::make_error_code(code::peer_certificate_invalid)) {
      return code::peer_certificate_invalid;
   }
   if (actual == forge::exceptions::make_error_code(code::certificate_chain_untrusted)) {
      return code::certificate_chain_untrusted;
   }
   if (actual == forge::exceptions::make_error_code(code::hostname_mismatch)) {
      return code::hostname_mismatch;
   }
   if (actual == forge::exceptions::make_error_code(code::fingerprint_mismatch)) {
      return code::fingerprint_mismatch;
   }
   if (actual == forge::exceptions::make_error_code(code::peer_rejected)) {
      return code::peer_rejected;
   }
   return std::nullopt;
}

[[nodiscard]] inline bool is(const forge::exceptions::base& value, code expected) noexcept {
   return value.code() == forge::exceptions::make_error_code(expected);
}

} // namespace forge::net::tls::exceptions
