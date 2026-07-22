module;

#include <bls12-381/bls12-381.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

module forge.crypto.bls.primitives;

namespace forge::crypto::bls::primitives {
namespace {

constexpr auto failure = std::int32_t{-1};
constexpr auto success = std::int32_t{0};

void run_yield(const yield_function& yield) {
   if (yield) {
      yield();
   }
}

} // namespace

std::int32_t g1_add(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right,
                    std::span<std::uint8_t> result) {
   if (left.size() != 96U || right.size() != 96U || result.size() != 96U) {
      return failure;
   }
   auto a = bls12_381::g1::fromAffineBytesLE(std::span<const std::uint8_t, 96>{left.data(), 96U},
                                             {.check_valid = true, .to_mont = true});
   auto b = bls12_381::g1::fromAffineBytesLE(std::span<const std::uint8_t, 96>{right.data(), 96U},
                                             {.check_valid = true, .to_mont = true});
   if (!a || !b) {
      return failure;
   }
   a->add(*b).toAffineBytesLE(std::span<std::uint8_t, 96>{result.data(), 96U}, bls12_381::from_mont::yes);
   return success;
}

std::int32_t g2_add(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right,
                    std::span<std::uint8_t> result) {
   if (left.size() != 192U || right.size() != 192U || result.size() != 192U) {
      return failure;
   }
   auto a = bls12_381::g2::fromAffineBytesLE(std::span<const std::uint8_t, 192>{left.data(), 192U},
                                             {.check_valid = true, .to_mont = true});
   auto b = bls12_381::g2::fromAffineBytesLE(std::span<const std::uint8_t, 192>{right.data(), 192U},
                                             {.check_valid = true, .to_mont = true});
   if (!a || !b) {
      return failure;
   }
   a->add(*b).toAffineBytesLE(std::span<std::uint8_t, 192>{result.data(), 192U}, bls12_381::from_mont::yes);
   return success;
}

std::int32_t g1_weighted_sum(std::span<const std::uint8_t> points, std::span<const std::uint8_t> scalars,
                             std::uint32_t count, std::span<std::uint8_t> result, yield_function yield) {
   if (count == 0U || points.size() != static_cast<std::size_t>(count) * 96U ||
       scalars.size() != static_cast<std::size_t>(count) * 32U || result.size() != 96U) {
      return failure;
   }
   auto parsed_points = std::vector<bls12_381::g1>{};
   auto parsed_scalars = std::vector<std::array<std::uint64_t, 4>>{};
   parsed_points.reserve(count);
   parsed_scalars.reserve(count);
   for (auto index = std::uint32_t{0}; index < count; ++index) {
      auto point = bls12_381::g1::fromAffineBytesLE(
          std::span<const std::uint8_t, 96>{points.data() + static_cast<std::size_t>(index) * 96U, 96U},
          {.check_valid = true, .to_mont = true});
      if (!point) {
         return failure;
      }
      parsed_points.push_back(*point);
      parsed_scalars.push_back(bls12_381::scalar::fromBytesLE<4>(
          std::span<const std::uint8_t, 32>{scalars.data() + static_cast<std::size_t>(index) * 32U, 32U}));
      if (index % 10U == 0U) {
         run_yield(yield);
      }
   }
   auto value = count == 1U ? parsed_points.front().scale(parsed_scalars.front())
                            : bls12_381::g1::weightedSum(parsed_points, parsed_scalars, [&] { run_yield(yield); });
   value.toAffineBytesLE(std::span<std::uint8_t, 96>{result.data(), 96U}, bls12_381::from_mont::yes);
   return success;
}

std::int32_t g2_weighted_sum(std::span<const std::uint8_t> points, std::span<const std::uint8_t> scalars,
                             std::uint32_t count, std::span<std::uint8_t> result, yield_function yield) {
   if (count == 0U || points.size() != static_cast<std::size_t>(count) * 192U ||
       scalars.size() != static_cast<std::size_t>(count) * 32U || result.size() != 192U) {
      return failure;
   }
   auto parsed_points = std::vector<bls12_381::g2>{};
   auto parsed_scalars = std::vector<std::array<std::uint64_t, 4>>{};
   parsed_points.reserve(count);
   parsed_scalars.reserve(count);
   for (auto index = std::uint32_t{0}; index < count; ++index) {
      auto point = bls12_381::g2::fromAffineBytesLE(
          std::span<const std::uint8_t, 192>{points.data() + static_cast<std::size_t>(index) * 192U, 192U},
          {.check_valid = true, .to_mont = true});
      if (!point) {
         return failure;
      }
      parsed_points.push_back(*point);
      parsed_scalars.push_back(bls12_381::scalar::fromBytesLE<4>(
          std::span<const std::uint8_t, 32>{scalars.data() + static_cast<std::size_t>(index) * 32U, 32U}));
      if (index % 6U == 0U) {
         run_yield(yield);
      }
   }
   auto value = count == 1U ? parsed_points.front().scale(parsed_scalars.front())
                            : bls12_381::g2::weightedSum(parsed_points, parsed_scalars, [&] { run_yield(yield); });
   value.toAffineBytesLE(std::span<std::uint8_t, 192>{result.data(), 192U}, bls12_381::from_mont::yes);
   return success;
}

std::int32_t pairing(std::span<const std::uint8_t> g1_points, std::span<const std::uint8_t> g2_points,
                     std::uint32_t count, std::span<std::uint8_t> result, yield_function yield) {
   if (count == 0U || g1_points.size() != static_cast<std::size_t>(count) * 96U ||
       g2_points.size() != static_cast<std::size_t>(count) * 192U || result.size() != 576U) {
      return failure;
   }
   auto pairs = std::vector<std::tuple<bls12_381::g1, bls12_381::g2>>{};
   pairs.reserve(count);
   for (auto index = std::uint32_t{0}; index < count; ++index) {
      auto g1 = bls12_381::g1::fromAffineBytesLE(
          std::span<const std::uint8_t, 96>{g1_points.data() + static_cast<std::size_t>(index) * 96U, 96U},
          {.check_valid = true, .to_mont = true});
      auto g2 = bls12_381::g2::fromAffineBytesLE(
          std::span<const std::uint8_t, 192>{g2_points.data() + static_cast<std::size_t>(index) * 192U, 192U},
          {.check_valid = true, .to_mont = true});
      if (!g1 || !g2) {
         return failure;
      }
      bls12_381::pairing::add_pair(pairs, *g1, *g2);
      if (index % 4U == 0U) {
         run_yield(yield);
      }
   }
   auto value = bls12_381::pairing::calculate(pairs, [&] { run_yield(yield); });
   value.toBytesLE(std::span<std::uint8_t, 576>{result.data(), 576U}, bls12_381::from_mont::yes);
   return success;
}

std::int32_t g1_map(std::span<const std::uint8_t> element, std::span<std::uint8_t> result) {
   if (element.size() != 48U || result.size() != 96U) {
      return failure;
   }
   auto value = bls12_381::fp::fromBytesLE(std::span<const std::uint8_t, 48>{element.data(), 48U},
                                           {.check_valid = true, .to_mont = true});
   if (!value) {
      return failure;
   }
   bls12_381::g1::mapToCurve(*value).toAffineBytesLE(std::span<std::uint8_t, 96>{result.data(), 96U},
                                                     bls12_381::from_mont::yes);
   return success;
}

std::int32_t g2_map(std::span<const std::uint8_t> element, std::span<std::uint8_t> result) {
   if (element.size() != 96U || result.size() != 192U) {
      return failure;
   }
   auto value = bls12_381::fp2::fromBytesLE(std::span<const std::uint8_t, 96>{element.data(), 96U},
                                            {.check_valid = true, .to_mont = true});
   if (!value) {
      return failure;
   }
   bls12_381::g2::mapToCurve(*value).toAffineBytesLE(std::span<std::uint8_t, 192>{result.data(), 192U},
                                                     bls12_381::from_mont::yes);
   return success;
}

std::int32_t field_mod(std::span<const std::uint8_t> scalar, std::span<std::uint8_t> result) {
   if (scalar.size() != 64U || result.size() != 48U) {
      return failure;
   }
   const auto wide = bls12_381::scalar::fromBytesLE<8>(std::span<const std::uint8_t, 64>{scalar.data(), 64U});
   auto value = bls12_381::fp::modPrime<8>(wide);
   value.toBytesLE(std::span<std::uint8_t, 48>{result.data(), 48U}, bls12_381::from_mont::yes);
   return success;
}

std::int32_t field_multiply(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right,
                            std::span<std::uint8_t> result) {
   if (left.size() != 48U || right.size() != 48U || result.size() != 48U) {
      return failure;
   }
   auto a = bls12_381::fp::fromBytesLE(std::span<const std::uint8_t, 48>{left.data(), 48U},
                                       {.check_valid = true, .to_mont = true});
   auto b = bls12_381::fp::fromBytesLE(std::span<const std::uint8_t, 48>{right.data(), 48U},
                                       {.check_valid = true, .to_mont = true});
   if (!a || !b) {
      return failure;
   }
   a->multiply(*b).toBytesLE(std::span<std::uint8_t, 48>{result.data(), 48U}, bls12_381::from_mont::yes);
   return success;
}

std::int32_t field_exponentiate(std::span<const std::uint8_t> base, std::span<const std::uint8_t> exponent,
                                std::span<std::uint8_t> result) {
   if (base.size() != 48U || exponent.size() != 64U || result.size() != 48U) {
      return failure;
   }
   auto value = bls12_381::fp::fromBytesLE(std::span<const std::uint8_t, 48>{base.data(), 48U},
                                           {.check_valid = true, .to_mont = true});
   if (!value) {
      return failure;
   }
   const auto power = bls12_381::scalar::fromBytesLE<8>(std::span<const std::uint8_t, 64>{exponent.data(), 64U});
   value->exp<8>(power).toBytesLE(std::span<std::uint8_t, 48>{result.data(), 48U}, bls12_381::from_mont::yes);
   return success;
}

} // namespace forge::crypto::bls::primitives
