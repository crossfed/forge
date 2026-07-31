module;

#include <algorithm>
#include <any>
#include <cstdint>
#include <exception>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module forge.config.core.decode;

import forge.config.core.component;
import forge.config.core.document;
import forge.config.core.value;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;

export namespace forge::config::core {

[[nodiscard]] bool parse_bool_text(std::string text, bool& output);
[[nodiscard]] schema::input_value to_schema_value(const value& input);
[[nodiscard]] value from_schema_value(const schema::input_value& input);
[[nodiscard]] std::any value_to_any(const value& input, schema::value_kind kind);
[[nodiscard]] value any_to_value(schema::value_kind kind, const std::any& input);

template <typename T> [[nodiscard]] component_descriptor describe_component(std::string section) {
   auto descriptor = component_descriptor{.section = std::move(section)};
   const auto rules = schema::rules<T>::define();
   for (const auto& field : rules.fields()) {
      descriptor.fields.push_back(field_descriptor{
          .name = field.name,
          .aliases = field.aliases,
          .kind = field.kind,
          .required = field.required,
          .has_default = field.has_default,
          .default_value = field.has_default && field.default_input
                              ? from_schema_value(field.default_input(field.default_value))
                              : field.has_default ? any_to_value(field.kind, field.default_value) : value{},
          .secret = field.secret,
          .deprecated = field.deprecated,
          .deprecated_message = field.deprecated_message,
          .description = field.description,
      });
   }
   return descriptor;
}

struct decode_diagnostics {
   std::vector<schema::diagnostic> entries;

   [[nodiscard]] bool ok() const {
      return std::ranges::none_of(
          entries, [](const schema::diagnostic& entry) { return entry.level == schema::severity::error; });
   }
};

[[nodiscard]] std::string format_decode_diagnostics(std::string_view prefix, const decode_diagnostics& diagnostics);

template <typename T> struct decode_result {
   T value{};
   decode_diagnostics diagnostics;

   [[nodiscard]] bool ok() const {
      return diagnostics.ok();
   }
};

template <typename T> [[nodiscard]] decode_result<T> decode(const document& source, std::string_view section = {}) {
   auto result = decode_result<T>{};
   const auto rules = schema::rules<T>::define();
   rules.apply_defaults(result.value);

   auto input = schema::input_value::object_type{};
   if (const auto* object = source.object_at(section)) {
      const auto converted = to_schema_value(value{*object});
      input = *converted.as_object();
   }
   result.diagnostics.entries = rules.decode_object(input, section, result.value);
   return result;
}

template <typename T> [[nodiscard]] document encode(const T& source, std::string_view section = {}) {
   auto output = document{};
   const auto rules = schema::rules<T>::define();
   auto value = from_schema_value(schema::input_value{rules.encode_object(source)});
   if (section.empty()) {
      output.root = std::move(*value.as_object());
   } else {
      output.set(std::string{section}, std::move(value));
   }
   return output;
}

template <typename T> [[nodiscard]] document defaults_for(std::string_view section) {
   auto output = document{};
   const auto rules = schema::rules<T>::define();
   for (const auto& field : rules.fields()) {
      if (!field.has_default) {
         continue;
      }
      auto field_path = std::string{section};
      if (!field_path.empty()) {
         field_path += ".";
      }
      field_path += field.name;
      output.set(std::move(field_path),
                 field.default_input ? from_schema_value(field.default_input(field.default_value))
                                     : any_to_value(field.kind, field.default_value));
   }
   return output;
}

} // namespace forge::config::core
