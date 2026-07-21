module;

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module forge.codec.base58;

export import forge.codec.base58.exceptions;

export namespace forge::codec::base58 {

[[nodiscard]] std::string encode(std::span<const std::uint8_t> input);
[[nodiscard]] std::vector<std::uint8_t> decode(std::string_view input);

} // namespace forge::codec::base58
