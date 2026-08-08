#include <boost/test/unit_test.hpp>

#include <array>
#include <string_view>

#include "authenticated_benchmark_options.hpp"

namespace benchmark = forge::test::db_authenticated::benchmark;

BOOST_AUTO_TEST_CASE(authenticated_benchmark_profile_defaults_bound_committed_versions) {
   constexpr auto one_million_arguments = std::array{std::string_view{"--baseline"}, std::string_view{"1m"}};
   const auto one_million = benchmark::parse_options(one_million_arguments);
   BOOST_TEST(one_million.keys == 1'000'000U);
   BOOST_TEST(one_million.load_chunk_keys == 32'768U);
   BOOST_TEST(!one_million.load_chunk_keys_overridden);
   BOOST_TEST(benchmark::chunk_keys_source(one_million) == "profile_default");
   BOOST_TEST(benchmark::committed_version_count(one_million.keys, one_million.load_chunk_keys) == 31U);

   constexpr auto ten_million_arguments = std::array{std::string_view{"--baseline=10m"}};
   const auto ten_million = benchmark::parse_options(ten_million_arguments);
   BOOST_TEST(ten_million.keys == 10'000'000U);
   BOOST_TEST(ten_million.load_chunk_keys == 65'536U);
   BOOST_TEST(!ten_million.load_chunk_keys_overridden);
   BOOST_TEST(benchmark::chunk_keys_source(ten_million) == "profile_default");
   BOOST_TEST(benchmark::committed_version_count(ten_million.keys, ten_million.load_chunk_keys) == 153U);
}

BOOST_AUTO_TEST_CASE(authenticated_benchmark_chunk_override_is_order_independent) {
   constexpr auto before_baseline = std::array{std::string_view{"--chunk-keys"}, std::string_view{"4096"},
                                               std::string_view{"--baseline"}, std::string_view{"10m"}};
   const auto before = benchmark::parse_options(before_baseline);
   BOOST_TEST(before.load_chunk_keys == 4'096U);
   BOOST_TEST(before.load_chunk_keys_overridden);
   BOOST_TEST(benchmark::chunk_keys_source(before) == "override");
   BOOST_TEST(benchmark::committed_version_count(before.keys, before.load_chunk_keys) == 2'442U);

   constexpr auto after_baseline =
       std::array{std::string_view{"--baseline=1m"}, std::string_view{"--chunk-keys=125000"}};
   const auto after = benchmark::parse_options(after_baseline);
   BOOST_TEST(after.load_chunk_keys == 125'000U);
   BOOST_TEST(after.load_chunk_keys_overridden);
   BOOST_TEST(benchmark::chunk_keys_source(after) == "override");
   BOOST_TEST(benchmark::committed_version_count(after.keys, after.load_chunk_keys) == 8U);
}

BOOST_AUTO_TEST_CASE(authenticated_benchmark_custom_profile_keeps_bounded_default) {
   constexpr auto arguments = std::array{std::string_view{"--keys"}, std::string_view{"10000"}};
   const auto settings = benchmark::parse_options(arguments);
   BOOST_CHECK(settings.baseline == benchmark::baseline_profile::custom);
   BOOST_TEST(settings.load_chunk_keys == 4'096U);
   BOOST_TEST(benchmark::chunk_keys_source(settings) == "custom_default");
   BOOST_TEST(benchmark::committed_version_count(settings.keys, settings.load_chunk_keys) == 3U);
}
