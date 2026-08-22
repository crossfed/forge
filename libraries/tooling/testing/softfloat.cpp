module;

#include <cstdint>
#include <cstring>
#include <bit>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#include <softfloat.hpp>

module forge.tooling.testing.host;

import forge.tooling.testing.schema;

#include "details/softfloat.hxx"

namespace forge::tooling::testing {

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

std::string format(float128 value) {
   auto stream = std::ostringstream{};
   stream.setf(std::ios::scientific, std::ios::floatfield);
   const auto number = float128_t{{value.words[0], value.words[1]}};
#if defined(__x86_64__)
   stream.precision(std::numeric_limits<long double>::digits10);
   auto approximate = extFloat80_t{};
   f128M_to_extF80M(&number, &approximate);
   auto native = static_cast<long double>(0);
   std::memcpy(&native, &approximate, sizeof(native));
   stream << native;
#else
   stream.precision(std::numeric_limits<double>::digits10);
   const auto approximate = f128M_to_f64(&number);
   stream << std::bit_cast<double>(approximate.v);
#endif
   return stream.str();
}

} // namespace forge::tooling::testing
