module;

#define BOOST_TEST_MODULE forge_contract_testing_tests
#include <boost/test/included/unit_test.hpp>

#include <cstdint>

module forge.contract.testing.host;

import forge.contract.testing.exceptions;
import forge.contract.testing.schema;
import forge.db.object.index;
import forge.vm.wasm.host_function;

#include "details/compiler_builtins.hxx"

namespace {

constexpr auto binary128_one_high = std::uint64_t{0x3fff000000000000ULL};
constexpr auto binary128_quiet_nan_high = std::uint64_t{0x7fff800000000000ULL};

} // namespace

BOOST_AUTO_TEST_CASE(quad_comparison_builtins_preserve_compiler_rt_unordered_results) {
   const auto builtins = forge::contract::testing::compiler_builtins{};

   BOOST_TEST(builtins.__gttf2(0, binary128_quiet_nan_high, 0, binary128_one_high) == -1);
   BOOST_TEST(builtins.__gttf2(0, binary128_one_high, 0, binary128_quiet_nan_high) == -1);
   BOOST_TEST(builtins.__lttf2(0, binary128_quiet_nan_high, 0, binary128_one_high) == 1);
   BOOST_TEST(builtins.__lttf2(0, binary128_one_high, 0, binary128_quiet_nan_high) == 1);
}

BOOST_AUTO_TEST_CASE(nan_sort_keys_report_typed_database_errors) {
   const auto double_nan = forge::contract::testing::float64{.bits = 0x7ff8'0000'0000'0000ULL};
   const auto quad_nan =
       forge::contract::testing::float128{.words = {0U, binary128_quiet_nan_high}};

   BOOST_CHECK_THROW(static_cast<void>(forge::db::object::sort_key<forge::contract::testing::float64>{}(double_nan)),
                     forge::contract::testing::exceptions::database_error);
   BOOST_CHECK_THROW(static_cast<void>(forge::db::object::sort_key<forge::contract::testing::float128>{}(quad_nan)),
                     forge::contract::testing::exceptions::database_error);
}
