module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module forge.codec.yaml;

import forge.config.core.key_path;
import forge.config.core.value;
import forge.config.core.document;
import forge.config.core.component;
import forge.config.core.decode;
import forge.config.core.migration;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;
import forge.variant.schema;

export namespace forge::codec::yaml {

enum class unknown_field_policy {
   ignore,
   warn,
   error,
};

struct read_options {
   std::string source_name;
   std::size_t max_depth = 128;
   unknown_field_policy unknown_fields = unknown_field_policy::warn;
};

struct write_options {
   bool flow_style = false;
   std::size_t max_bytes = std::numeric_limits<std::size_t>::max();
   std::chrono::system_clock::time_point deadline = std::chrono::system_clock::time_point::max();
};

template <typename T> struct read_result {
   T value{};
   std::vector<schema::diagnostic> diagnostics;

   [[nodiscard]] bool ok() const {
      return std::ranges::none_of(
          diagnostics, [](const schema::diagnostic& entry) { return entry.level == schema::severity::error; });
   }
};

struct write_result {
   std::string text;
   std::vector<schema::diagnostic> diagnostics;

   [[nodiscard]] bool ok() const {
      return std::ranges::none_of(
          diagnostics, [](const schema::diagnostic& entry) { return entry.level == schema::severity::error; });
   }
};

[[nodiscard]] read_result<variant> read_value(std::string_view input, read_options options = {});
[[nodiscard]] write_result write_value(const variant& input, write_options options = {});
[[nodiscard]] read_result<config::core::document> read_document(std::string_view input, read_options options = {});
[[nodiscard]] write_result write_document(const config::core::document& input, write_options options = {});

[[nodiscard]] read_result<variant> load_value(const std::filesystem::path& path, read_options options = {});
[[nodiscard]] write_result save_value(const std::filesystem::path& path, const variant& input,
                                      write_options options = {});
[[nodiscard]] read_result<config::core::document> load_document(const std::filesystem::path& path,
                                                                read_options options = {});
[[nodiscard]] write_result save_document(const std::filesystem::path& path, const config::core::document& input,
                                         write_options options = {});

} // namespace forge::codec::yaml

namespace forge::codec::yaml::detail {

inline void append_schema_diagnostics(std::vector<schema::diagnostic>& output,
                                      std::vector<schema::diagnostic> diagnostics,
                                      unknown_field_policy unknown_fields) {
   for (auto& entry : diagnostics) {
      if (entry.code == "config.unknown") {
         if (unknown_fields == unknown_field_policy::ignore) {
            continue;
         }
         entry.code = "yaml.unknown";
         if (unknown_fields == unknown_field_policy::error) {
            entry.level = schema::severity::error;
         }
      } else if (entry.code == "config.missing") {
         entry.code = "yaml.missing";
      } else if (entry.code == "config.duplicate") {
         entry.code = "yaml.duplicate";
      } else if (entry.code == "config.type") {
         entry.code = "yaml.type";
      } else if (entry.code == "config.range") {
         entry.code = "yaml.range";
      }
      output.push_back(std::move(entry));
   }
}

template <typename T>
[[nodiscard]] bool materialize_schema_records(variant& source, const read_options& options,
                                              std::vector<schema::diagnostic>& diagnostics) {
   append_schema_diagnostics(diagnostics, variant_schema::materialize<T>(source), options.unknown_fields);
   return std::ranges::none_of(diagnostics,
                               [](const schema::diagnostic& entry) { return entry.level == schema::severity::error; });
}

} // namespace forge::codec::yaml::detail

export namespace forge::codec::yaml {

template <typename T> [[nodiscard]] read_result<T> read(std::string_view input, read_options options = {}) {
   auto output = read_result<T>{};
   const auto rules = schema::rules<T>::define();
   if (!rules.fields().empty()) {
      auto parsed_document = read_document(input, options);
      output.diagnostics = std::move(parsed_document.diagnostics);
      if (!parsed_document.ok()) {
         return output;
      }
      auto decoded = config::core::decode<T>(parsed_document.value);
      output.value = std::move(decoded.value);
      for (auto entry : std::move(decoded.diagnostics.entries)) {
         if (entry.code == "config.unknown") {
            if (options.unknown_fields == unknown_field_policy::ignore) {
               continue;
            }
            entry.code = "yaml.unknown";
            if (options.unknown_fields == unknown_field_policy::error) {
               entry.level = schema::severity::error;
            }
         }
         output.diagnostics.push_back(std::move(entry));
      }
      return output;
   }

   auto parsed = read_value(input, options);
   output.diagnostics = std::move(parsed.diagnostics);
   if (!parsed.ok()) {
      return output;
   }

   rules.apply_defaults(output.value);
   if (!detail::materialize_schema_records<T>(parsed.value, options, output.diagnostics)) {
      return output;
   }

   if constexpr (requires(const variant& source, T& target) { from_variant(source, target); }) {
      try {
         from_variant(parsed.value, output.value);
      } catch (const std::exception& error) {
         output.diagnostics.push_back(schema::diagnostic{
             .path = {},
             .code = "yaml.type",
             .level = schema::severity::error,
             .message = error.what(),
         });
         return output;
      }
   } else {
      output.diagnostics.push_back(schema::diagnostic{
          .path = {},
          .code = "yaml.type",
          .level = schema::severity::error,
          .message = "type is not readable from YAML without schema rules or forge::from_variant",
      });
      return output;
   }

   if (options.unknown_fields != unknown_field_policy::ignore && parsed.value.is_object()) {
      auto known = std::set<std::string>{};
      for (const auto& field : rules.fields()) {
         known.insert(field.name);
         known.insert(field.aliases.begin(), field.aliases.end());
      }
      if (!known.empty()) {
         for (const auto& entry : parsed.value.get_object()) {
            if (!known.contains(entry.key())) {
               output.diagnostics.push_back(schema::diagnostic{
                   .path = entry.key(),
                   .code = "yaml.unknown",
                   .level = options.unknown_fields == unknown_field_policy::error ? schema::severity::error
                                                                                  : schema::severity::warning,
                   .message = "unknown YAML field",
               });
            }
         }
      }
   }

   auto validation = rules.validate(output.value);
   output.diagnostics.insert(output.diagnostics.end(), validation.begin(), validation.end());
   return output;
}

template <typename T> [[nodiscard]] read_result<T> load(const std::filesystem::path& path, read_options options = {}) {
   auto parsed = load_value(path, options);
   auto output = read_result<T>{};
   output.diagnostics = std::move(parsed.diagnostics);
   if (!parsed.ok()) {
      return output;
   }

   const auto rules = schema::rules<T>::define();
   if (!rules.fields().empty()) {
      auto parsed_document = load_document(path, options);
      output.diagnostics = std::move(parsed_document.diagnostics);
      if (!parsed_document.ok()) {
         return output;
      }
      auto decoded = config::core::decode<T>(parsed_document.value);
      output.value = std::move(decoded.value);
      for (auto entry : std::move(decoded.diagnostics.entries)) {
         if (entry.code == "config.unknown") {
            if (options.unknown_fields == unknown_field_policy::ignore) {
               continue;
            }
            entry.code = "yaml.unknown";
            if (options.unknown_fields == unknown_field_policy::error) {
               entry.level = schema::severity::error;
            }
         }
         output.diagnostics.push_back(std::move(entry));
      }
      return output;
   }

   rules.apply_defaults(output.value);
   if (!detail::materialize_schema_records<T>(parsed.value, options, output.diagnostics)) {
      return output;
   }
   if constexpr (requires(const variant& source, T& target) { from_variant(source, target); }) {
      try {
         from_variant(parsed.value, output.value);
      } catch (const std::exception& error) {
         output.diagnostics.push_back(schema::diagnostic{
             .path = {},
             .code = "yaml.type",
             .level = schema::severity::error,
             .message = error.what(),
         });
         return output;
      }
   } else {
      output.diagnostics.push_back(schema::diagnostic{
          .path = {},
          .code = "yaml.type",
          .level = schema::severity::error,
          .message = "type is not readable from YAML without schema rules or forge::from_variant",
      });
      return output;
   }

   auto validation = rules.validate(output.value);
   output.diagnostics.insert(output.diagnostics.end(), validation.begin(), validation.end());
   return output;
}

template <typename T> [[nodiscard]] write_result write(const T& input, write_options options = {}) {
   const auto rules = schema::rules<T>::define();
   if (!rules.fields().empty()) {
      try {
         return write_document(config::core::encode(input), std::move(options));
      } catch (const std::exception& error) {
         return write_result{
             .diagnostics = {schema::diagnostic{
                 .path = {},
                 .code = "yaml.type",
                 .level = schema::severity::error,
                 .message = error.what(),
             }},
         };
      }
   }
   if constexpr (requires(const T& source, variant& output) { to_variant(source, output); }) {
      return write_value(variant_schema::encode(input), std::move(options));
   } else {
      return write_result{
          .diagnostics = {schema::diagnostic{
              .path = {},
              .code = "yaml.type",
              .level = schema::severity::error,
              .message = "type is not writable to YAML without schema rules or forge::to_variant",
          }},
      };
   }
}

template <typename T>
[[nodiscard]] write_result save(const std::filesystem::path& path, const T& input, write_options options = {}) {
   const auto rules = schema::rules<T>::define();
   if (!rules.fields().empty()) {
      try {
         return save_document(path, config::core::encode(input), std::move(options));
      } catch (const std::exception& error) {
         return write_result{
             .diagnostics = {schema::diagnostic{
                 .path = {},
                 .code = "yaml.type",
                 .level = schema::severity::error,
                 .message = error.what(),
             }},
         };
      }
   }
   if constexpr (requires(const T& source, variant& output) { to_variant(source, output); }) {
      return save_value(path, variant_schema::encode(input), std::move(options));
   } else {
      return write_result{
          .diagnostics = {schema::diagnostic{
              .path = {},
              .code = "yaml.type",
              .level = schema::severity::error,
              .message = "type is not writable to YAML without schema rules or forge::to_variant",
          }},
      };
   }
}

} // namespace forge::codec::yaml
