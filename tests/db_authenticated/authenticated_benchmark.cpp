#include <boost/asio/awaitable.hpp>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

import forge.asio.affine;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.authenticated.codec;
import forge.db.authenticated.store;
import forge.db.authenticated.transaction;
import forge.db.authenticated.types;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.mdbx.driver;

namespace {

using clock_type = std::chrono::steady_clock;
using forge::db::authenticated::bytes;

constexpr auto point_proof_count = std::size_t{1'000};
constexpr auto range_proof_count = std::size_t{100};
constexpr auto range_proof_limit = std::uint32_t{256};
constexpr auto mebibyte = std::uint64_t{1} << 20U;
constexpr auto gibibyte = std::uint64_t{1} << 30U;
constexpr auto tebibyte = std::uint64_t{1} << 40U;

enum class baseline_profile {
   custom,
   one_million,
   ten_million,
};

struct provisional_gate_thresholds {
   double initial_batch_min_keys_per_second = 0.0;
   double point_proofs_min_per_second = 0.0;
   double range_proofs_min_per_second = 0.0;
};

struct options {
   std::size_t keys = 10'000;
   std::size_t value_bytes = 32;
   std::filesystem::path path;
   std::string machine_label = "unspecified";
   baseline_profile baseline = baseline_profile::custom;
};

struct proof_measurement {
   std::chrono::nanoseconds elapsed{};
   std::uint64_t wire_bytes = 0;
   std::uint64_t nodes = 0;
};

struct benchmark_result {
   forge::db::authenticated::root root;
   std::chrono::nanoseconds initial_batch{};
   proof_measurement point_proofs;
   proof_measurement range_proofs;
};

class temporary_path_guard {
 public:
   temporary_path_guard(std::filesystem::path path, bool enabled) : path_{std::move(path)}, enabled_{enabled} {}

   ~temporary_path_guard() {
      if (enabled_) {
         auto error = std::error_code{};
         std::filesystem::remove_all(path_, error);
      }
   }

   temporary_path_guard(const temporary_path_guard&) = delete;
   temporary_path_guard& operator=(const temporary_path_guard&) = delete;

 private:
   std::filesystem::path path_;
   bool enabled_ = false;
};

[[noreturn]] void usage_error(std::string_view message) {
   throw std::invalid_argument{std::string{message} + "\nusage: benchmark_forge_db_authenticated "
                                                      "[--baseline 1m|10m | --keys N] [--value-bytes N] "
                                                      "[--machine-label LABEL] [--path PATH]"};
}

std::uint64_t parse_unsigned(std::string_view option, std::string_view value) {
   auto result = std::uint64_t{};
   const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
   if (error != std::errc{} || end != value.data() + value.size()) {
      usage_error(std::string{option} + " expects an unsigned integer");
   }
   return result;
}

std::optional<std::string_view> option_value(int& index, int argc, char** argv, std::string_view option) {
   const auto argument = std::string_view{argv[index]};
   if (argument == option) {
      if (++index >= argc) {
         usage_error(std::string{option} + " requires a value");
      }
      return std::string_view{argv[index]};
   }
   const auto prefix = std::string{option} + '=';
   if (argument.starts_with(prefix)) {
      return argument.substr(prefix.size());
   }
   return {};
}

options parse_options(int argc, char** argv) {
   auto result = options{};
   auto has_baseline = false;
   auto has_explicit_keys = false;
   for (auto index = 1; index < argc; ++index) {
      const auto argument = std::string_view{argv[index]};
      if (argument == "--help") {
         std::cout << "usage: benchmark_forge_db_authenticated "
                      "[--baseline 1m|10m | --keys N] [--value-bytes N] "
                      "[--machine-label LABEL] [--path PATH]\n";
         std::exit(0);
      }
      if (const auto value = option_value(index, argc, argv, "--baseline")) {
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
      if (const auto value = option_value(index, argc, argv, "--keys")) {
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
      if (const auto value = option_value(index, argc, argv, "--value-bytes")) {
         const auto parsed = parse_unsigned("--value-bytes", *value);
         if (parsed > forge::db::authenticated::limits{}.max_value_bytes) {
            usage_error("--value-bytes exceeds the authenticated-store limit");
         }
         result.value_bytes = static_cast<std::size_t>(parsed);
         continue;
      }
      if (const auto value = option_value(index, argc, argv, "--machine-label")) {
         if (value->empty()) {
            usage_error("--machine-label requires a non-empty value");
         }
         result.machine_label = *value;
         continue;
      }
      if (const auto value = option_value(index, argc, argv, "--path")) {
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
   return result;
}

std::string_view baseline_name(baseline_profile baseline) {
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

std::optional<provisional_gate_thresholds> provisional_thresholds(baseline_profile baseline) {
   switch (baseline) {
   case baseline_profile::custom:
      return std::nullopt;
   case baseline_profile::one_million:
   case baseline_profile::ten_million:
      // These intentionally broad floors detect stalled or grossly regressed runs. They are not product SLOs.
      return provisional_gate_thresholds{
          .initial_batch_min_keys_per_second = 250.0,
          .point_proofs_min_per_second = 2.0,
          .range_proofs_min_per_second = 0.2,
      };
   }
   throw std::logic_error{"unknown authenticated benchmark baseline"};
}

bytes make_key(std::uint64_t index) {
   auto result = bytes(sizeof(index));
   for (auto offset = std::size_t{}; offset < result.size(); ++offset) {
      const auto shift = static_cast<unsigned>((result.size() - offset - 1U) * 8U);
      result[offset] = static_cast<std::byte>((index >> shift) & 0xffU);
   }
   return result;
}

bytes make_value(std::uint64_t index, std::size_t size) {
   auto result = bytes(size);
   auto state = index ^ 0x9e3779b97f4a7c15ULL;
   for (auto& value : result) {
      state ^= state >> 12U;
      state ^= state << 25U;
      state ^= state >> 27U;
      value = static_cast<std::byte>((state * 0x2545f4914f6cdd1dULL) >> 56U);
   }
   return result;
}

std::vector<forge::db::authenticated::mutation> make_mutations(const options& settings) {
   auto result = std::vector<forge::db::authenticated::mutation>{};
   result.reserve(settings.keys);
   for (auto index = std::size_t{}; index < settings.keys; ++index) {
      result.push_back({
          .key = make_key(index),
          .value = make_value(index, settings.value_bytes),
      });
   }
   return result;
}

std::size_t sample_position(std::size_t sample, std::size_t samples, std::size_t population) {
   const auto quotient = population / samples;
   const auto remainder = population % samples;
   return quotient * sample + (remainder * sample) / samples;
}

std::size_t inclusive_sample_position(std::size_t sample, std::size_t samples, std::size_t maximum) {
   if (samples <= 1U) {
      return 0U;
   }
   const auto denominator = samples - 1U;
   const auto quotient = maximum / denominator;
   const auto remainder = maximum % denominator;
   return quotient * sample + (remainder * sample) / denominator;
}

std::uint64_t mdbx_growth_step(std::size_t keys) {
   return keys >= 1'000'000U ? gibibyte : 64U * mebibyte;
}

void require(bool condition, std::string_view message) {
   if (!condition) {
      throw std::runtime_error{std::string{message}};
   }
}

boost::asio::awaitable<benchmark_result> run_benchmark(const options& settings,
                                                       const std::shared_ptr<forge::db::mdbx::driver>& driver) {
   auto authenticated = forge::db::authenticated::store{
       driver,
       {
           .family = forge::db::core::family{"authenticated"},
           .domain = "forge.benchmark.db.authenticated.v1",
       },
   };
   auto mutations = make_mutations(settings);
   auto result = benchmark_result{};

   const auto initial_started = clock_type::now();
   auto db_transaction = co_await driver->begin_transaction();
   auto authenticated_transaction = co_await authenticated.join(db_transaction, 1);
   const auto staged = co_await authenticated_transaction.stage(mutations);
   co_await db_transaction.commit();
   result.initial_batch = clock_type::now() - initial_started;
   result.root = staged.commitment;

   mutations.clear();
   mutations.shrink_to_fit();

   for (auto sample = std::size_t{}; sample < point_proof_count; ++sample) {
      const auto key = make_key(sample_position(sample, point_proof_count, settings.keys));
      const auto started = clock_type::now();
      const auto proof = co_await authenticated.prove(result.root.version, key, false);
      result.point_proofs.elapsed += clock_type::now() - started;
      require(proof.anchor == result.root, "point proof is not bound to the committed root");
      require(proof.terminal && proof.terminal->key == key, "point proof does not contain the requested key");
      result.point_proofs.wire_bytes += forge::db::authenticated::wire_size(proof);
   }

   const auto maximum_range_start = settings.keys > range_proof_limit ? settings.keys - range_proof_limit : 0U;
   for (auto sample = std::size_t{}; sample < range_proof_count; ++sample) {
      const auto lower = make_key(inclusive_sample_position(sample, range_proof_count, maximum_range_start));
      const auto request = forge::db::authenticated::range_request{
          .lower = lower,
          .limit = range_proof_limit,
          .include_values = false,
      };
      const auto started = clock_type::now();
      const auto proof = co_await authenticated.prove_range(result.root.version, request);
      result.range_proofs.elapsed += clock_type::now() - started;
      require(proof.anchor == result.root, "range proof is not bound to the committed root");
      require(proof.request == request, "range proof does not preserve the requested bounds");
      result.range_proofs.wire_bytes += forge::db::authenticated::wire_size(proof);
      result.range_proofs.nodes += proof.nodes.size();
   }

   co_return result;
}

double milliseconds(std::chrono::nanoseconds elapsed) {
   return std::chrono::duration<double, std::milli>{elapsed}.count();
}

double operations_per_second(std::size_t operations, std::chrono::nanoseconds elapsed) {
   const auto seconds = std::chrono::duration<double>{elapsed}.count();
   return seconds == 0.0 ? 0.0 : static_cast<double>(operations) / seconds;
}

std::string json_escape(std::string_view value) {
   constexpr auto hex = std::string_view{"0123456789abcdef"};
   auto result = std::string{};
   result.reserve(value.size());
   for (const auto character : value) {
      const auto byte = static_cast<unsigned char>(character);
      switch (character) {
      case '"':
         result += "\\\"";
         break;
      case '\\':
         result += "\\\\";
         break;
      case '\b':
         result += "\\b";
         break;
      case '\f':
         result += "\\f";
         break;
      case '\n':
         result += "\\n";
         break;
      case '\r':
         result += "\\r";
         break;
      case '\t':
         result += "\\t";
         break;
      default:
         if (byte < 0x20U) {
            result += "\\u00";
            result += hex[(byte >> 4U) & 0x0fU];
            result += hex[byte & 0x0fU];
         } else {
            result += character;
         }
      }
   }
   return result;
}

bool print_result(const options& settings, const std::filesystem::path& path, const benchmark_result& result) {
   const auto initial_batch_rate = operations_per_second(settings.keys, result.initial_batch);
   const auto point_proof_rate = operations_per_second(point_proof_count, result.point_proofs.elapsed);
   const auto range_proof_rate = operations_per_second(range_proof_count, result.range_proofs.elapsed);
   const auto thresholds = provisional_thresholds(settings.baseline);
   const auto initial_batch_passed = !thresholds || initial_batch_rate >= thresholds->initial_batch_min_keys_per_second;
   const auto point_proofs_passed = !thresholds || point_proof_rate >= thresholds->point_proofs_min_per_second;
   const auto range_proofs_passed = !thresholds || range_proof_rate >= thresholds->range_proofs_min_per_second;
   const auto gate_passed = initial_batch_passed && point_proofs_passed && range_proofs_passed;

   std::cout << std::fixed << std::setprecision(3) << "{\n"
             << "  \"format\": \"forge.db.authenticated.benchmark.v1\",\n"
             << "  \"benchmark\": \"forge_db_authenticated\",\n"
             << "  \"config\": {\n"
             << "    \"baseline\": \"" << baseline_name(settings.baseline) << "\",\n"
             << "    \"machine_label\": \"" << json_escape(settings.machine_label) << "\",\n"
             << "    \"keys\": " << settings.keys << ",\n"
             << "    \"value_bytes\": " << settings.value_bytes << ",\n"
             << "    \"path\": \"" << json_escape(path.string()) << "\",\n"
             << "    \"mdbx_upper_bytes\": " << tebibyte << ",\n"
             << "    \"mdbx_growth_bytes\": " << mdbx_growth_step(settings.keys) << ",\n"
             << "    \"point_proof_count\": " << point_proof_count << ",\n"
             << "    \"range_proof_count\": " << range_proof_count << ",\n"
             << "    \"range_proof_limit\": " << range_proof_limit << "\n"
             << "  },\n"
             << "  \"root\": {\n"
             << "    \"version\": " << result.root.version << ",\n"
             << "    \"state_root\": \"" << result.root.state_root.str() << "\",\n"
             << "    \"state_size\": " << result.root.state_size << ",\n"
             << "    \"change_root\": \"" << result.root.change_root.str() << "\",\n"
             << "    \"change_count\": " << result.root.change_count << "\n"
             << "  },\n"
             << "  \"metrics\": {\n"
             << "    \"initial_batch\": {\n"
             << "      \"elapsed_ms\": " << milliseconds(result.initial_batch) << ",\n"
             << "      \"keys_per_second\": " << initial_batch_rate << "\n"
             << "    },\n"
             << "    \"point_proofs\": {\n"
             << "      \"elapsed_ms\": " << milliseconds(result.point_proofs.elapsed) << ",\n"
             << "      \"proofs_per_second\": " << point_proof_rate << ",\n"
             << "      \"average_wire_bytes\": "
             << static_cast<double>(result.point_proofs.wire_bytes) / point_proof_count << "\n"
             << "    },\n"
             << "    \"range_proofs\": {\n"
             << "      \"elapsed_ms\": " << milliseconds(result.range_proofs.elapsed) << ",\n"
             << "      \"proofs_per_second\": " << range_proof_rate << ",\n"
             << "      \"average_wire_bytes\": "
             << static_cast<double>(result.range_proofs.wire_bytes) / range_proof_count << ",\n"
             << "      \"average_nodes\": " << static_cast<double>(result.range_proofs.nodes) / range_proof_count
             << "\n"
             << "    }\n"
             << "  },\n"
             << "  \"acceptance\": {\n"
             << "    \"classification\": \"provisional_measurement_gate\",\n"
             << "    \"latency_slo\": false,\n"
             << "    \"status\": \"" << (thresholds ? (gate_passed ? "pass" : "fail") : "not_evaluated") << "\",\n";
   if (thresholds) {
      std::cout << "    \"thresholds\": {\n"
                << "      \"initial_batch_min_keys_per_second\": " << thresholds->initial_batch_min_keys_per_second
                << ",\n"
                << "      \"point_proofs_min_per_second\": " << thresholds->point_proofs_min_per_second << ",\n"
                << "      \"range_proofs_min_per_second\": " << thresholds->range_proofs_min_per_second << "\n"
                << "    },\n"
                << "    \"checks\": {\n"
                << "      \"initial_batch\": " << (initial_batch_passed ? "true" : "false") << ",\n"
                << "      \"point_proofs\": " << (point_proofs_passed ? "true" : "false") << ",\n"
                << "      \"range_proofs\": " << (range_proofs_passed ? "true" : "false") << "\n"
                << "    }\n";
   } else {
      std::cout << "    \"thresholds\": null,\n"
                << "    \"checks\": null\n";
   }
   std::cout << "  }\n"
             << "}\n";
   return gate_passed;
}

} // namespace

int main(int argc, char** argv) try {
   const auto settings = parse_options(argc, argv);
   const auto temporary_path = settings.path.empty();
   const auto path =
       temporary_path
           ? std::filesystem::temp_directory_path() /
                 ("forge_db_authenticated_benchmark_" + std::to_string(clock_type::now().time_since_epoch().count()))
           : std::filesystem::absolute(settings.path);
   if (std::filesystem::exists(path)) {
      usage_error("--path must name a path that does not exist");
   }
   if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path());
   }
   [[maybe_unused]] auto path_guard = temporary_path_guard{path, temporary_path};
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{{.thread_name = "authenticated-benchmark"}};
   auto result = benchmark_result{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await forge::db::mdbx::driver::open(
          forge::db::mdbx::config{
              .path = path.string(),
              .families = {"authenticated"},
              .map =
                  {
                      .upper_size = tebibyte,
                      .growth_step = mdbx_growth_step(settings.keys),
                  },
          },
          lane.get_executor());
      result = co_await run_benchmark(settings, driver);
      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());

   const auto gate_passed = print_result(settings, path, result);
   return gate_passed ? 0 : 2;
} catch (const std::exception& error) {
   std::cerr << "benchmark failed: " << error.what() << '\n';
   return 1;
}
