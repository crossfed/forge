module;
#include <forge/exceptions/macros.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module forge.crypto.base58;

import forge.core.string;
import forge.core.utility;
export import forge.exceptions;
import forge.crypto.types;

export namespace forge::crypto::base58::exceptions {

enum class code : std::uint16_t {
   invalid_character = 1,
   backend_error = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.base58")

using invalid_character = forge::exceptions::coded_exception<code, code::invalid_character>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace forge::crypto::base58::exceptions

export namespace forge::crypto {

[[nodiscard]] std::string base58_encode(std::span<const std::uint8_t> data, const forge::yield_function_t& yield = {});
[[nodiscard]] bytes base58_decode(std::string_view base58_str);
} // namespace forge::crypto
