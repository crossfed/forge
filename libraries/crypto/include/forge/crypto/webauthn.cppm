module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.crypto.webauthn;

import forge.crypto.asymmetric.value;
import forge.crypto.sha256;
export import forge.exceptions;

export namespace forge::crypto::webauthn {

namespace exceptions {

enum class code : std::uint16_t {
   invalid_client_data = 1,
   invalid_signature = 2,
   invalid_options = 3,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.webauthn")

using invalid_client_data = forge::exceptions::coded_exception<code, code::invalid_client_data>;
using invalid_signature = forge::exceptions::coded_exception<code, code::invalid_signature>;
using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;

} // namespace exceptions

[[nodiscard]] bool valid(const asymmetric::webauthn_public_key& key) noexcept;
[[nodiscard]] asymmetric::webauthn_public_key recover(const asymmetric::webauthn_signature& value, const sha256& digest,
                                                      bool check_canonical = true);

} // namespace forge::crypto::webauthn
