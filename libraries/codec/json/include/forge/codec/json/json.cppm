module;

#include <boost/multi_index_container.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <flat_map>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

export module forge.codec.json;

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
import forge.reflect.reflect;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;
import forge.variant.static_variant;

namespace forge::codec::json::detail {

template <typename T> using clean_type = std::remove_cv_t<std::remove_reference_t<T>>;

inline constexpr auto dynamic_sequence_extent = std::numeric_limits<std::size_t>::max();

template <typename T> struct optional_traits {
   static constexpr bool value = false;
};

template <typename T> struct optional_traits<std::optional<T>> {
   static constexpr bool value = true;
   using value_type = T;
};

template <typename T> inline constexpr bool is_optional_v = optional_traits<clean_type<T>>::value;

template <typename T> struct sequence_traits {
   static constexpr bool value = false;
};

template <typename T, typename Allocator> struct sequence_traits<std::vector<T, Allocator>> {
   static constexpr bool value = !std::same_as<T, char>;
   static constexpr std::size_t extent = dynamic_sequence_extent;
   using value_type = T;
};

template <typename T, typename Allocator> struct sequence_traits<std::deque<T, Allocator>> {
   static constexpr bool value = true;
   static constexpr std::size_t extent = dynamic_sequence_extent;
   using value_type = T;
};

template <typename T, std::size_t Size> struct sequence_traits<std::array<T, Size>> {
   static constexpr bool value = true;
   static constexpr std::size_t extent = Size;
   using value_type = T;
};

template <typename T, typename Compare, typename Allocator> struct sequence_traits<std::set<T, Compare, Allocator>> {
   static constexpr bool value = true;
   static constexpr std::size_t extent = dynamic_sequence_extent;
   using value_type = T;
};

template <typename T, typename Hash, typename Equal, typename Allocator>
struct sequence_traits<std::unordered_set<T, Hash, Equal, Allocator>> {
   static constexpr bool value = true;
   static constexpr std::size_t extent = dynamic_sequence_extent;
   using value_type = T;
};

template <typename T> inline constexpr bool is_sequence_v = sequence_traits<clean_type<T>>::value;

template <typename T> struct multi_index_traits {
   static constexpr bool value = false;
};

template <typename Value, typename IndexSpecifierList, typename Allocator>
struct multi_index_traits<boost::multi_index_container<Value, IndexSpecifierList, Allocator>> {
   static constexpr bool value = true;
   using value_type = Value;
};

template <typename T> inline constexpr bool is_multi_index_v = multi_index_traits<clean_type<T>>::value;

template <typename T> struct unique_sequence_traits {
   static constexpr bool value = false;
};

template <typename T, typename Compare, typename Allocator>
struct unique_sequence_traits<std::set<T, Compare, Allocator>> {
   static constexpr bool value = true;
   using seen_type = std::set<T, Compare>;
};

template <typename T, typename Hash, typename Equal, typename Allocator>
struct unique_sequence_traits<std::unordered_set<T, Hash, Equal, Allocator>> {
   static constexpr bool value = true;
   using seen_type = std::unordered_set<T, Hash, Equal>;
};

template <typename T> struct pair_traits {
   static constexpr bool value = false;
};

template <typename First, typename Second> struct pair_traits<std::pair<First, Second>> {
   static constexpr bool value = true;
   using first_type = First;
   using second_type = Second;
};

template <typename T> inline constexpr bool is_pair_v = pair_traits<clean_type<T>>::value;

template <typename T> struct associative_traits {
   static constexpr bool value = false;
};

template <typename Key, typename Value, typename Compare, typename Allocator>
struct associative_traits<std::map<Key, Value, Compare, Allocator>> {
   static constexpr bool value = true;
   static constexpr bool unique = true;
   using key_type = Key;
   using mapped_type = Value;
   using seen_type = std::set<Key, Compare>;
};

template <typename Key, typename Value, typename Compare, typename Allocator>
struct associative_traits<std::multimap<Key, Value, Compare, Allocator>> {
   static constexpr bool value = true;
   static constexpr bool unique = false;
   using key_type = Key;
   using mapped_type = Value;
   using seen_type = std::set<Key, Compare>;
};

template <typename Key, typename Value, typename Hash, typename Equal, typename Allocator>
struct associative_traits<std::unordered_map<Key, Value, Hash, Equal, Allocator>> {
   static constexpr bool value = true;
   static constexpr bool unique = true;
   using key_type = Key;
   using mapped_type = Value;
   using seen_type = std::unordered_set<Key, Hash, Equal>;
};

template <typename Key, typename Value, typename Compare, typename KeyContainer, typename MappedContainer>
struct associative_traits<std::flat_map<Key, Value, Compare, KeyContainer, MappedContainer>> {
   static constexpr bool value = true;
   static constexpr bool unique = true;
   using key_type = Key;
   using mapped_type = Value;
   using seen_type = std::set<Key, Compare>;
};

template <typename T> inline constexpr bool is_associative_v = associative_traits<clean_type<T>>::value;

template <typename T> struct variant_traits {
   static constexpr bool value = false;
};

template <typename... T> struct variant_traits<std::variant<T...>> {
   static constexpr bool value = true;
   static constexpr std::size_t size = sizeof...(T);
};

template <typename T> inline constexpr bool is_variant_v = variant_traits<clean_type<T>>::value;

[[nodiscard]] inline std::string field_path(std::string_view path, std::string_view field) {
   if (path.empty()) {
      return std::string{field};
   }
   return std::string{path} + "." + std::string{field};
}

[[nodiscard]] inline std::string element_path(std::string_view path, std::size_t index) {
   return std::string{path} + "[" + std::to_string(index) + "]";
}

inline void add_exact_error(std::vector<schema::diagnostic>& diagnostics, std::string path, std::string code,
                            std::string message) {
   diagnostics.push_back(schema::diagnostic{
       .path = path.empty() ? "$" : std::move(path),
       .code = std::move(code),
       .level = schema::severity::error,
       .message = std::move(message),
   });
}

template <typename T>
void validate_exact(const variant& source, std::string_view path, std::vector<schema::diagnostic>& diagnostics);

template <typename Variant, std::size_t Index = 0>
void validate_variant_payload(std::size_t selected, const variant& payload, std::string_view path,
                              std::vector<schema::diagnostic>& diagnostics) {
   if constexpr (Index < std::variant_size_v<Variant>) {
      if (selected == Index) {
         validate_exact<std::variant_alternative_t<Index, Variant>>(payload, path, diagnostics);
         return;
      }
      validate_variant_payload<Variant, Index + 1>(selected, payload, path, diagnostics);
   }
}

template <typename T>
void validate_exact(const variant& source, std::string_view path, std::vector<schema::diagnostic>& diagnostics) {
   using value_type = clean_type<T>;

   if constexpr (is_optional_v<value_type>) {
      if (!source.is_null()) {
         validate_exact<typename optional_traits<value_type>::value_type>(source, path, diagnostics);
      }
   } else if constexpr (reflect::is_described_object_v<value_type>) {
      // Some described value types intentionally use a canonical string adapter.
      // Their own from_variant overload remains the authority for that scalar form.
      if (source.is_string()) {
         return;
      }
      if (!source.is_object()) {
         add_exact_error(diagnostics, std::string{path}, "json.object", "described record must be a JSON object");
         return;
      }

      const auto& object = source.get_object();
      const auto rules = schema::rules<value_type>::define();
      auto known = std::set<std::string>{};
      for (const auto& field : rules.fields()) {
         known.emplace(field.name);
         known.insert(field.aliases.begin(), field.aliases.end());
      }

      reflect::for_each_member<value_type>([&](const char* name, auto member) {
         const auto rule = std::ranges::find_if(
             rules.fields(), [name](const auto& field) { return field.member_name == std::string_view{name}; });
         auto expected_name = std::string{name};
         auto found = object.end();
         if (rule != rules.fields().end()) {
            expected_name = rule->name;
            found = object.find(rule->name);
            for (auto alias = rule->aliases.begin(); found == object.end() && alias != rule->aliases.end(); ++alias) {
               found = object.find(*alias);
            }
         } else {
            known.emplace(name);
            found = object.find(name);
         }

         using member_type = clean_type<decltype(std::declval<value_type>().*member)>;
         if (found == object.end()) {
            if constexpr (!is_optional_v<member_type>) {
               add_exact_error(diagnostics, field_path(path, expected_name), "json.missing", "missing JSON field");
            }
            return;
         }
         validate_exact<member_type>(found->value(), field_path(path, found->key()), diagnostics);
      });

      for (const auto& entry : object) {
         if (!known.contains(entry.key())) {
            add_exact_error(diagnostics, field_path(path, entry.key()), "json.unknown", "unknown JSON field");
         }
      }
   } else if constexpr (is_variant_v<value_type>) {
      // Public-key and signature variants use canonical string encodings.
      if (source.is_string()) {
         return;
      }
      if (!source.is_array()) {
         add_exact_error(diagnostics, std::string{path}, "json.variant", "variant must be encoded as [index, payload]");
         return;
      }

      const auto& elements = source.get_array();
      if (elements.size() != 2U) {
         add_exact_error(diagnostics, std::string{path}, "json.variant",
                         "variant must contain exactly an index and payload");
         return;
      }
      if (!elements[0].is_integer()) {
         add_exact_error(diagnostics, element_path(path, 0U), "json.variant", "variant index must be an integer");
         return;
      }

      if (elements[0].is_int64() && elements[0].as_int64() < 0) {
         add_exact_error(diagnostics, element_path(path, 0U), "json.variant", "variant index is out of range");
         return;
      }

      const auto selected = elements[0].as_uint64();
      if (selected >= variant_traits<value_type>::size) {
         add_exact_error(diagnostics, element_path(path, 0U), "json.variant", "variant index is out of range");
         return;
      }
      validate_variant_payload<value_type>(selected, elements[1], element_path(path, 1U), diagnostics);
   } else if constexpr (is_multi_index_v<value_type>) {
      if (!source.is_array()) {
         add_exact_error(diagnostics, std::string{path}, "json.array", "multi-index container must be a JSON array");
         return;
      }

      const auto& elements = source.get_array();
      auto seen = value_type{};
      for (std::size_t index = 0; index < elements.size(); ++index) {
         const auto entry_path = element_path(path, index);
         validate_exact<typename multi_index_traits<value_type>::value_type>(elements[index], entry_path, diagnostics);
         try {
            const auto value = elements[index].template as<typename multi_index_traits<value_type>::value_type>();
            const auto previous_size = seen.size();
            seen.insert(value);
            if (seen.size() == previous_size) {
               add_exact_error(diagnostics, entry_path, "json.duplicate",
                               "element violates a unique multi-index constraint");
            }
         } catch (const std::exception&) {
            // Conversion reports the canonical type diagnostic after structural validation.
         }
      }
   } else if constexpr (is_associative_v<value_type>) {
      if (!source.is_array()) {
         add_exact_error(diagnostics, std::string{path}, "json.array", "associative container must be a JSON array");
         return;
      }

      const auto& elements = source.get_array();
      auto seen = typename associative_traits<value_type>::seen_type{};
      for (std::size_t index = 0; index < elements.size(); ++index) {
         const auto entry_path = element_path(path, index);
         validate_exact<std::pair<typename associative_traits<value_type>::key_type,
                                  typename associative_traits<value_type>::mapped_type>>(elements[index], entry_path,
                                                                                         diagnostics);
         if constexpr (associative_traits<value_type>::unique) {
            if (!elements[index].is_array() || elements[index].get_array().size() != 2U) {
               continue;
            }
            try {
               const auto key =
                   elements[index].get_array()[0].template as<typename associative_traits<value_type>::key_type>();
               if (!seen.insert(key).second) {
                  add_exact_error(diagnostics, element_path(entry_path, 0U), "json.duplicate",
                                  "duplicate key in unique associative container");
               }
            } catch (const std::exception&) {
               // Conversion reports the canonical type diagnostic after structural validation.
            }
         }
      }
   } else if constexpr (is_pair_v<value_type>) {
      if (!source.is_array() || source.get_array().size() != 2U) {
         add_exact_error(diagnostics, std::string{path}, "json.pair", "pair must contain exactly a key and value");
         return;
      }

      const auto& elements = source.get_array();
      validate_exact<typename pair_traits<value_type>::first_type>(elements[0], element_path(path, 0U), diagnostics);
      validate_exact<typename pair_traits<value_type>::second_type>(elements[1], element_path(path, 1U), diagnostics);
   } else if constexpr (is_sequence_v<value_type>) {
      if (!source.is_array()) {
         add_exact_error(diagnostics, std::string{path}, "json.array", "sequence must be a JSON array");
         return;
      }

      const auto& elements = source.get_array();
      if constexpr (sequence_traits<value_type>::extent != dynamic_sequence_extent) {
         if (elements.size() != sequence_traits<value_type>::extent) {
            add_exact_error(diagnostics, std::string{path}, "json.array", "JSON array has an unexpected size");
            return;
         }
      }
      for (std::size_t index = 0; index < elements.size(); ++index) {
         validate_exact<typename sequence_traits<value_type>::value_type>(elements[index], element_path(path, index),
                                                                          diagnostics);
      }
      if constexpr (unique_sequence_traits<value_type>::value) {
         auto seen = typename unique_sequence_traits<value_type>::seen_type{};
         for (std::size_t index = 0; index < elements.size(); ++index) {
            try {
               const auto value = elements[index].template as<typename sequence_traits<value_type>::value_type>();
               if (!seen.insert(value).second) {
                  add_exact_error(diagnostics, element_path(path, index), "json.duplicate",
                                  "duplicate element in unique sequence");
               }
            } catch (const std::exception&) {
               // Conversion reports the canonical type diagnostic after structural validation.
            }
         }
      }
   }
}

} // namespace forge::codec::json::detail

export namespace forge::codec::json {

enum class unknown_field_policy {
   ignore,
   warn,
   error,
};

enum class described_record_policy {
   permissive,
   exact,
};

struct read_options {
   std::string source_name;
   std::size_t max_depth = 128;
   unknown_field_policy unknown_fields = unknown_field_policy::warn;
   described_record_policy described_records = described_record_policy::permissive;
};

struct write_options {
   bool pretty = false;
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

template <typename T> [[nodiscard]] read_result<T> read(std::string_view input, read_options options = {}) {
   auto output = read_result<T>{};
   const auto rules = schema::rules<T>::define();
   if (!rules.fields().empty()) {
      auto parsed_document = read_document(input, options);
      output.diagnostics = std::move(parsed_document.diagnostics);
      if (!parsed_document.ok()) {
         return output;
      }
      if (options.described_records == described_record_policy::exact) {
         const auto input = config::core::to_schema_value(config::core::value{parsed_document.value.root});
         auto exact = rules.validate_exact_input(*input.as_object());
         for (auto& entry : exact) {
            if (entry.code == "config.unknown") {
               entry.code = "json.unknown";
            } else if (entry.code == "config.missing") {
               entry.code = "json.missing";
            } else if (entry.code == "config.duplicate") {
               entry.code = "json.duplicate";
            } else if (entry.code == "config.type") {
               entry.code = "json.type";
            }
            output.diagnostics.push_back(std::move(entry));
         }
         if (!output.ok()) {
            return output;
         }
      }
      auto decoded = config::core::decode<T>(parsed_document.value);
      output.value = std::move(decoded.value);
      for (auto entry : std::move(decoded.diagnostics.entries)) {
         if (entry.code == "config.unknown") {
            if (options.unknown_fields == unknown_field_policy::ignore) {
               continue;
            }
            entry.code = "json.unknown";
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

   if (options.described_records == described_record_policy::exact) {
      detail::validate_exact<T>(parsed.value, {}, output.diagnostics);
      if (!output.ok()) {
         return output;
      }
   }

   if constexpr (requires(const variant& source, T& target) { from_variant(source, target); }) {
      try {
         from_variant(parsed.value, output.value);
      } catch (const std::exception& error) {
         output.diagnostics.push_back(schema::diagnostic{
             .path = {},
             .code = "json.type",
             .level = schema::severity::error,
             .message = error.what(),
         });
         return output;
      }
   } else {
      output.diagnostics.push_back(schema::diagnostic{
          .path = {},
          .code = "json.type",
          .level = schema::severity::error,
          .message = "type is not readable from JSON without schema rules or forge::from_variant",
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
                   .code = "json.unknown",
                   .level = options.unknown_fields == unknown_field_policy::error ? schema::severity::error
                                                                                  : schema::severity::warning,
                   .message = "unknown JSON field",
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
      if (options.described_records == described_record_policy::exact) {
         const auto input = config::core::to_schema_value(config::core::value{parsed_document.value.root});
         auto exact = rules.validate_exact_input(*input.as_object());
         for (auto& entry : exact) {
            if (entry.code == "config.unknown") {
               entry.code = "json.unknown";
            } else if (entry.code == "config.missing") {
               entry.code = "json.missing";
            } else if (entry.code == "config.duplicate") {
               entry.code = "json.duplicate";
            } else if (entry.code == "config.type") {
               entry.code = "json.type";
            }
            output.diagnostics.push_back(std::move(entry));
         }
         if (!output.ok()) {
            return output;
         }
      }
      auto decoded = config::core::decode<T>(parsed_document.value);
      output.value = std::move(decoded.value);
      for (auto entry : std::move(decoded.diagnostics.entries)) {
         if (entry.code == "config.unknown") {
            if (options.unknown_fields == unknown_field_policy::ignore) {
               continue;
            }
            entry.code = "json.unknown";
            if (options.unknown_fields == unknown_field_policy::error) {
               entry.level = schema::severity::error;
            }
         }
         output.diagnostics.push_back(std::move(entry));
      }
      return output;
   }

   rules.apply_defaults(output.value);
   if (options.described_records == described_record_policy::exact) {
      detail::validate_exact<T>(parsed.value, {}, output.diagnostics);
      if (!output.ok()) {
         return output;
      }
   }

   if constexpr (requires(const variant& source, T& target) { from_variant(source, target); }) {
      try {
         from_variant(parsed.value, output.value);
      } catch (const std::exception& error) {
         output.diagnostics.push_back(schema::diagnostic{
             .path = {},
             .code = "json.type",
             .level = schema::severity::error,
             .message = error.what(),
         });
         return output;
      }
   } else {
      output.diagnostics.push_back(schema::diagnostic{
          .path = {},
          .code = "json.type",
          .level = schema::severity::error,
          .message = "type is not readable from JSON without schema rules or forge::from_variant",
      });
      return output;
   }

   auto validation = rules.validate(output.value);
   output.diagnostics.insert(output.diagnostics.end(), validation.begin(), validation.end());
   return output;
}

template <typename T> [[nodiscard]] write_result write(const T& input, write_options options = {}) {
   return write_value(variant{input}, std::move(options));
}

template <typename T>
[[nodiscard]] write_result save(const std::filesystem::path& path, const T& input, write_options options = {}) {
   return save_value(path, variant{input}, std::move(options));
}

} // namespace forge::codec::json
