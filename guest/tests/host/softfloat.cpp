module;

#include <cstdint>
#include <cstring>

#include <softfloat.hpp>

module forge.contract.testing.host;

import forge.contract.testing.schema;

#include "details/softfloat.hxx"

namespace forge::contract::testing {

bool is_nan(float64 value) noexcept {
   const auto number = float64_t{value.bits};
   return !f64_eq(number, number);
}

bool is_nan(float128 value) noexcept {
   const auto number = float128_t{{value.words[0], value.words[1]}};
   return !f128_eq(number, number);
}

bool equal(float64 left, float64 right) noexcept {
   return f64_eq(float64_t{left.bits}, float64_t{right.bits});
}

bool equal(float128 left, float128 right) noexcept {
   return f128_eq(float128_t{{left.words[0], left.words[1]}}, float128_t{{right.words[0], right.words[1]}});
}

} // namespace forge::contract::testing
