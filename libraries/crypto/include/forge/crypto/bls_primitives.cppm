module;

#include <cstdint>
#include <functional>
#include <span>

export module forge.crypto.bls.primitives;

export namespace forge::crypto::bls::primitives {

using yield_function = std::function<void()>;

[[nodiscard]] std::int32_t g1_add(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right,
                                  std::span<std::uint8_t> result);
[[nodiscard]] std::int32_t g2_add(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right,
                                  std::span<std::uint8_t> result);
[[nodiscard]] std::int32_t g1_weighted_sum(std::span<const std::uint8_t> points, std::span<const std::uint8_t> scalars,
                                           std::uint32_t count, std::span<std::uint8_t> result,
                                           yield_function yield = {});
[[nodiscard]] std::int32_t g2_weighted_sum(std::span<const std::uint8_t> points, std::span<const std::uint8_t> scalars,
                                           std::uint32_t count, std::span<std::uint8_t> result,
                                           yield_function yield = {});
[[nodiscard]] std::int32_t pairing(std::span<const std::uint8_t> g1_points, std::span<const std::uint8_t> g2_points,
                                   std::uint32_t count, std::span<std::uint8_t> result, yield_function yield = {});
[[nodiscard]] std::int32_t g1_map(std::span<const std::uint8_t> element, std::span<std::uint8_t> result);
[[nodiscard]] std::int32_t g2_map(std::span<const std::uint8_t> element, std::span<std::uint8_t> result);
[[nodiscard]] std::int32_t field_mod(std::span<const std::uint8_t> scalar, std::span<std::uint8_t> result);
[[nodiscard]] std::int32_t field_multiply(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right,
                                          std::span<std::uint8_t> result);
[[nodiscard]] std::int32_t field_exponentiate(std::span<const std::uint8_t> base,
                                              std::span<const std::uint8_t> exponent, std::span<std::uint8_t> result);

} // namespace forge::crypto::bls::primitives
