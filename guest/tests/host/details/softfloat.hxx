#pragma once

namespace forge::contract::testing {

bool is_nan(float64 value) noexcept;
bool is_nan(float128 value) noexcept;
bool equal(float64 left, float64 right) noexcept;
bool equal(float128 left, float128 right) noexcept;
std::string format(float128 value);

} // namespace forge::contract::testing
