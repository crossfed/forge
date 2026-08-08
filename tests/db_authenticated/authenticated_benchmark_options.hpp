#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace forge::test::db_authenticated::benchmark {

inline constexpr auto custom_chunk_keys = std::size_t{4'096};
// Scaled runs select power-of-two chunks that cap profile history at 31/153 durable versions.
inline constexpr auto one_million_chunk_keys = std::size_t{32'768};
inline constexpr auto ten_million_chunk_keys = std::size_t{65'536};

enum class baseline_profile {
   custom,
   one_million,
   ten_million,
};

struct options {
   std::size_t keys = 10'000;
   std::size_t value_bytes = 32;
   std::size_t load_chunk_keys = custom_chunk_keys;
   std::filesystem::path path;
   std::string machine_label = "unspecified";
   baseline_profile baseline = baseline_profile::custom;
   bool load_chunk_keys_overridden = false;
   bool help = false;
};

inline constexpr auto usage =
    std::string_view{"usage: benchmark_forge_db_authenticated [--baseline 1m|10m | --keys N] [--value-bytes N] "
                     "[--chunk-keys N] [--machine-label LABEL] [--path PATH]"};

[[noreturn]] inline void usage_error(std::string_view message) {
   throw std::invalid_argument{std::string{message} + '\n' + std::string{usage}};
}

inline std::uint64_t parse_unsigned(std::string_view option, std::string_view value) {
   auto result = std::uint64_t{};
   const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
   if (error != std::errc{} || end != value.data() + value.size()) {
      usage_error(std::string{option} + " expects an unsigned integer");
   }
   return result;
}

inline std::optional<std::string_view> option_value(std::size_t& index, std::span<const std::string_view> arguments,
                                                    std::string_view option) {
   const auto argument = arguments[index];
   if (argument == option) {
      if (++index >= arguments.size()) {
         usage_error(std::string{option} + " requires a value");
      }
      return arguments[index];
   }
   const auto prefix = std::string{option} + '=';
   if (argument.starts_with(prefix)) {
      return argument.substr(prefix.size());
   }
   return {};
}

inline constexpr std::size_t default_chunk_keys(baseline_profile baseline) {
   switch (baseline) {
   case baseline_profile::custom:
      return custom_chunk_keys;
   case baseline_profile::one_million:
      return one_million_chunk_keys;
   case baseline_profile::ten_million:
      return ten_million_chunk_keys;
   }
   throw std::logic_error{"unknown authenticated benchmark baseline"};
}

inline constexpr std::size_t committed_version_count(std::size_t keys, std::size_t chunk_keys) {
   return keys / chunk_keys + (keys % chunk_keys == 0U ? 0U : 1U);
}

inline std::string_view baseline_name(baseline_profile baseline) {
   switch (baseline) {
   case baseline_profile::custom:
      return "custom";
   case baseline_profile::one_million:
      return "1m";
   case baseline_profile::ten_million:
      return "10m";
   }
   throw std::logic_error{"unknown authenticated benchmark baseline"};
}

inline std::string_view chunk_keys_source(const options& settings) {
   if (settings.load_chunk_keys_overridden) {
      return "override";
   }
   return settings.baseline == baseline_profile::custom ? "custom_default" : "profile_default";
}

inline options parse_options(std::span<const std::string_view> arguments) {
   auto result = options{};
   auto has_baseline = false;
   auto has_explicit_keys = false;
   auto has_explicit_chunk_keys = false;
   for (auto index = std::size_t{}; index < arguments.size(); ++index) {
      const auto argument = arguments[index];
      if (argument == "--help") {
         result.help = true;
         return result;
      }
      if (const auto value = option_value(index, arguments, "--baseline")) {
         if (has_baseline) {
            usage_error("--baseline may only be specified once");
         }
         if (*value == "1m") {
            result.baseline = baseline_profile::one_million;
            result.keys = 1'000'000;
         } else if (*value == "10m") {
            result.baseline = baseline_profile::ten_million;
            result.keys = 10'000'000;
         } else {
            usage_error("--baseline expects 1m or 10m");
         }
         has_baseline = true;
         continue;
      }
      if (const auto value = option_value(index, arguments, "--keys")) {
         if (has_explicit_keys) {
            usage_error("--keys may only be specified once");
         }
         const auto parsed = parse_unsigned("--keys", *value);
         if (parsed == 0U || parsed > std::numeric_limits<std::size_t>::max()) {
            usage_error("--keys is outside the supported size_t range");
         }
         result.keys = static_cast<std::size_t>(parsed);
         has_explicit_keys = true;
         continue;
      }
      if (const auto value = option_value(index, arguments, "--value-bytes")) {
         const auto parsed = parse_unsigned("--value-bytes", *value);
         if (parsed > std::numeric_limits<std::size_t>::max()) {
            usage_error("--value-bytes is outside the supported size_t range");
         }
         result.value_bytes = static_cast<std::size_t>(parsed);
         continue;
      }
      if (const auto value = option_value(index, arguments, "--chunk-keys")) {
         if (has_explicit_chunk_keys) {
            usage_error("--chunk-keys may only be specified once");
         }
         const auto parsed = parse_unsigned("--chunk-keys", *value);
         if (parsed == 0U || parsed > std::numeric_limits<std::size_t>::max()) {
            usage_error("--chunk-keys is outside the supported size_t range");
         }
         result.load_chunk_keys = static_cast<std::size_t>(parsed);
         result.load_chunk_keys_overridden = true;
         has_explicit_chunk_keys = true;
         continue;
      }
      if (const auto value = option_value(index, arguments, "--machine-label")) {
         if (value->empty()) {
            usage_error("--machine-label requires a non-empty value");
         }
         result.machine_label = *value;
         continue;
      }
      if (const auto value = option_value(index, arguments, "--path")) {
         if (value->empty()) {
            usage_error("--path requires a non-empty value");
         }
         result.path = std::filesystem::path{*value};
         continue;
      }
      usage_error(std::string{"unknown option: "} + std::string{argument});
   }
   if (has_baseline && has_explicit_keys) {
      usage_error("--baseline and --keys are mutually exclusive");
   }
   if (!has_explicit_chunk_keys) {
      result.load_chunk_keys = default_chunk_keys(result.baseline);
   }
   return result;
}

} // namespace forge::test::db_authenticated::benchmark
