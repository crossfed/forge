module;

#include <cstdint>
#include <functional>
#include <span>

export module forge.crypto.bn256;

export namespace forge::crypto::bn256 {

using yield_function = std::function<void()>;

[[nodiscard]] std::int32_t add(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right,
                               std::span<std::uint8_t> result) noexcept;

[[nodiscard]] std::int32_t multiply(std::span<const std::uint8_t> point, std::span<const std::uint8_t> scalar,
                                    std::span<std::uint8_t> result) noexcept;

// Returns -1 for malformed input, 0 when the pairing is false, and 1 when it is true.
[[nodiscard]] std::int32_t pairing_check(std::span<const std::uint8_t> pairs, yield_function yield = {});

} // namespace forge::crypto::bn256
