module;
#include <forge/exceptions/macros.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module forge.codec.base32;

export import forge.exceptions;

export namespace forge::codec::base32::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.codec.base32")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;

} // namespace forge::codec::base32::exceptions

export namespace forge::codec::base32 {

enum class alphabet_case {
   lower,
   upper,
};

struct options {
   alphabet_case characters = alphabet_case::lower;
   bool padding = false;
};

[[nodiscard]] std::string encode(std::span<const std::uint8_t> data, options options = {});
[[nodiscard]] std::vector<std::uint8_t> decode(std::string_view value);

} // namespace forge::codec::base32
