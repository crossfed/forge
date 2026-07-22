module;

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module forge.codec.hex;

export import forge.codec.hex.exceptions;

namespace forge::codec::hex::detail {
void require_integer_width(std::size_t width);
}

export namespace forge::codec::hex {

enum class letter_case {
   lower,
   upper,
};

[[nodiscard]] std::string encode(std::span<const std::uint8_t> input, letter_case letters = letter_case::lower);
std::size_t decode(std::string_view input, std::span<std::uint8_t> output);
[[nodiscard]] std::vector<std::uint8_t> decode(std::string_view input);

template <std::unsigned_integral T>
[[nodiscard]] std::string encode(T value, std::size_t width = sizeof(T) * 2U,
                                 letter_case letters = letter_case::lower) {
   detail::require_integer_width(width);
   const auto digits =
       letters == letter_case::lower ? std::string_view{"0123456789abcdef"} : std::string_view{"0123456789ABCDEF"};
   auto result = std::string(width, '0');
   for (auto index = std::size_t{}; index < width; ++index) {
      const auto shift = (width - index - 1U) * 4U;
      result[index] = shift < sizeof(T) * 8U ? digits[static_cast<std::size_t>((value >> shift) & T{0x0f})] : '0';
   }
   return result;
}

} // namespace forge::codec::hex
