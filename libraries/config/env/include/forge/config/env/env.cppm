module;

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module forge.config.env;

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

export namespace forge::config::env {

enum class unknown_variable_policy {
   ignore,
   warn,
   error,
};

struct read_options {
   std::string prefix;
   std::string source_name = "env";
   unknown_variable_policy unknown_variables = unknown_variable_policy::warn;
   bool allow_aliases = true;
   bool strict_alias_conflicts = true;
   bool allow_deprecated = true;
   bool case_sensitive = false;
};

struct write_options {
   std::string prefix;
   bool include_comments = true;
   bool include_defaults = true;
   std::string secret_example_placeholder;
   std::string secret_value_placeholder = "<redacted>";
};

struct environment_variable {
   std::string name;
   std::string value;
   config::core::source_location location;
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

[[nodiscard]] std::string variable_name(std::string_view section, std::string_view field,
                                        const write_options& options);
[[nodiscard]] std::string variable_name(std::string_view section, std::string_view field,
                                        const read_options& options);
[[nodiscard]] std::optional<std::filesystem::path> home_directory();

[[nodiscard]] read_result<config::core::document> read_variables(const std::vector<environment_variable>& variables,
                                                           const config::core::component_registry& registry,
                                                           read_options options = {});
[[nodiscard]] read_result<config::core::document> read_document(std::string_view input,
                                                          const config::core::component_registry& registry,
                                                          read_options options = {});
[[nodiscard]] read_result<config::core::document> load_document(const std::filesystem::path& path,
                                                          const config::core::component_registry& registry,
                                                          read_options options = {});
[[nodiscard]] read_result<config::core::document> read_process_document(const config::core::component_registry& registry,
                                                                  read_options options = {});

[[nodiscard]] write_result write_document(const config::core::document& document, const config::core::component_registry& registry,
                                          write_options options = {});
[[nodiscard]] write_result write_example(const config::core::component_registry& registry, write_options options = {});
[[nodiscard]] write_result save_document(const std::filesystem::path& path, const config::core::document& document,
                                         const config::core::component_registry& registry, write_options options = {});
[[nodiscard]] write_result save_example(const std::filesystem::path& path, const config::core::component_registry& registry,
                                        write_options options = {});

template <typename T>
[[nodiscard]] read_result<T> read(std::string_view input, std::string section, read_options options = {}) {
   auto registry = config::core::component_registry{};
   registry.add(config::core::describe_component<T>(section));

   auto parsed = read_document(input, registry, std::move(options));
   auto output = read_result<T>{};
   output.diagnostics = std::move(parsed.diagnostics);
   if (!parsed.ok()) {
      return output;
   }

   auto decoded = config::core::decode<T>(parsed.value, section);
   output.value = std::move(decoded.value);
   output.diagnostics.insert(output.diagnostics.end(), decoded.diagnostics.entries.begin(),
                             decoded.diagnostics.entries.end());
   return output;
}

template <typename T>
[[nodiscard]] read_result<T> load(const std::filesystem::path& path, std::string section,
                                  read_options options = {}) {
   auto registry = config::core::component_registry{};
   registry.add(config::core::describe_component<T>(section));

   auto parsed = load_document(path, registry, std::move(options));
   auto output = read_result<T>{};
   output.diagnostics = std::move(parsed.diagnostics);
   if (!parsed.ok()) {
      return output;
   }

   auto decoded = config::core::decode<T>(parsed.value, section);
   output.value = std::move(decoded.value);
   output.diagnostics.insert(output.diagnostics.end(), decoded.diagnostics.entries.begin(),
                             decoded.diagnostics.entries.end());
   return output;
}

template <typename T>
[[nodiscard]] read_result<T> read_process(std::string section, read_options options = {}) {
   auto registry = config::core::component_registry{};
   registry.add(config::core::describe_component<T>(section));

   auto parsed = read_process_document(registry, std::move(options));
   auto output = read_result<T>{};
   output.diagnostics = std::move(parsed.diagnostics);
   if (!parsed.ok()) {
      return output;
   }

   auto decoded = config::core::decode<T>(parsed.value, section);
   output.value = std::move(decoded.value);
   output.diagnostics.insert(output.diagnostics.end(), decoded.diagnostics.entries.begin(),
                             decoded.diagnostics.entries.end());
   return output;
}

} // namespace forge::config::env
