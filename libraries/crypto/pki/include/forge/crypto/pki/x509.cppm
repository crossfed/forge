module;

#include <forge/exceptions/macros.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

export module forge.crypto.pki.x509;

import forge.crypto.asymmetric;
import forge.crypto.core.types;
export import forge.exceptions;

export namespace forge::crypto::pki::x509 {

namespace exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   backend_error = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.pki.x509")

using invalid_key = forge::exceptions::coded_exception<code, code::invalid_key>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace exceptions

class certificate {
 public:
   certificate() = default;
   explicit certificate(core::bytes der);

   [[nodiscard]] static certificate from_der(std::span<const std::uint8_t> bytes);
   [[nodiscard]] static certificate from_pem(std::string_view text);

   [[nodiscard]] const core::bytes& der() const noexcept;
   [[nodiscard]] core::bytes public_key_der() const;
   [[nodiscard]] asymmetric::public_key key() const;
   [[nodiscard]] core::bytes extension(std::string_view oid) const;
   [[nodiscard]] core::bytes fingerprint_sha256() const;
   [[nodiscard]] std::string fingerprint_sha256_text() const;

 private:
   core::bytes der_;
};

} // namespace forge::crypto::pki::x509
