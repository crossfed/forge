module;

#include <forge/exceptions/macros.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

export module forge.crypto.core.random;

export import forge.exceptions;
import forge.crypto.core.types;

export namespace forge::crypto::core::random::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   backend_error = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.core.random")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace forge::crypto::core::random::exceptions

export namespace forge::crypto::core {

void fill_random(std::span<std::uint8_t> out);

[[nodiscard]] bytes random_bytes(std::size_t size);

template <std::size_t Size> [[nodiscard]] std::array<std::uint8_t, Size> random_array() {
   auto out = std::array<std::uint8_t, Size>{};
   fill_random(out);
   return out;
}

} // namespace forge::crypto
