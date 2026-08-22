module;

#include <cstdint>
#include <span>

export module forge.crypto.core.constant_time;

export namespace forge::crypto::core {

[[nodiscard]] bool constant_time_equal(std::span<const std::uint8_t> left,
                                       std::span<const std::uint8_t> right) noexcept;

} // namespace forge::crypto::core
