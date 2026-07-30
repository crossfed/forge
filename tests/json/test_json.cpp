#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace forge_json_tests {

struct http_config {
   std::uint16_t bind_port = 0;
   std::string bind_host;
   bool tls_enabled = false;
   std::vector<std::string> tags;
};

struct named_tag {
   std::string value;

   named_tag() = default;
   explicit named_tag(std::string input) : value(std::move(input)) {}
};

struct tag_config {
   std::vector<named_tag> tags;
};

} // namespace forge_json_tests

#include <boost/describe.hpp>

BOOST_DESCRIBE_STRUCT(forge_json_tests::http_config, (), (bind_port, bind_host, tls_enabled, tags))
BOOST_DESCRIBE_STRUCT(forge_json_tests::named_tag, (), (value))
BOOST_DESCRIBE_STRUCT(forge_json_tests::tag_config, (), (tags))

import forge.config.core.key_path;
import forge.config.core.value;
import forge.config.core.document;
import forge.config.core.component;
import forge.config.core.decode;
import forge.config.core.migration;
import forge.codec.json;
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
import forge.tests.codec.json.exact_types;

template <> struct forge::schema::rules<forge_json_tests::http_config> {
   [[nodiscard]] static forge::schema::object_schema<forge_json_tests::http_config> define() {
      auto schema = forge::schema::object<forge_json_tests::http_config>();
      schema.field<&forge_json_tests::http_config::bind_port>("bind-port")
          .alias("port")
          .required()
          .default_value(8080)
          .range(1, 65535);
      schema.field<&forge_json_tests::http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&forge_json_tests::http_config::tls_enabled>("tls-enabled").default_value(false);
      static_cast<void>(schema.field<&forge_json_tests::http_config::tags>("tags"));
      return schema;
   }
};

template <> struct forge::schema::rules<forge_json_tests::named_tag> {
   [[nodiscard]] static forge::schema::object_schema<forge_json_tests::named_tag> define() {
      auto schema = forge::schema::object<forge_json_tests::named_tag>();
      static_cast<void>(schema.field<&forge_json_tests::named_tag::value>("value"));
      return schema;
   }
};

template <> struct forge::schema::rules<forge_json_tests::tag_config> {
   [[nodiscard]] static forge::schema::object_schema<forge_json_tests::tag_config> define() {
      auto schema = forge::schema::object<forge_json_tests::tag_config>();
      static_cast<void>(schema.field<&forge_json_tests::tag_config::tags>("tags"));
      return schema;
   }
};

template <> struct forge::schema::rules<forge_json_tests::exact_alias_leaf> {
   [[nodiscard]] static forge::schema::object_schema<forge_json_tests::exact_alias_leaf> define() {
      auto schema = forge::schema::object<forge_json_tests::exact_alias_leaf>();
      schema.field<&forge_json_tests::exact_alias_leaf::bind_port>("bind-port").alias("port").default_value(8080);
      return schema;
   }
};

BOOST_AUTO_TEST_SUITE(json_codec_tests)

BOOST_AUTO_TEST_CASE(json_value_roundtrip_preserves_generic_shapes) {
   const auto parsed =
       forge::codec::json::read_value(R"({"null":null,"flag":true,"i":-2,"u":7,"d":3.5,"s":"x","a":[1,"b"]})");
   BOOST_REQUIRE(parsed.ok());

   const auto& object = parsed.value.get_object();
   BOOST_TEST(object["flag"].as_bool());
   BOOST_TEST(object["i"].as_int64() == -2);
   BOOST_TEST(object["u"].as_uint64() == 7U);
   BOOST_TEST(object["d"].as_double() == 3.5);
   BOOST_TEST(object["s"].get_string() == "x");
   BOOST_REQUIRE_EQUAL(object["a"].get_array().size(), 2U);

   const auto written = forge::codec::json::write_value(parsed.value);
   BOOST_REQUIRE(written.ok());
   const auto reparsed = forge::codec::json::read_value(written.text);
   BOOST_REQUIRE(reparsed.ok());
   BOOST_TEST(reparsed.value.get_object()["flag"].as_bool());
   BOOST_TEST(reparsed.value.get_object()["i"].as_int64() == -2);
   BOOST_TEST(reparsed.value.get_object()["u"].as_uint64() == 7U);
   BOOST_REQUIRE_EQUAL(reparsed.value.get_object()["a"].get_array().size(), 2U);
}

BOOST_AUTO_TEST_CASE(json_large_uint64_is_not_silently_converted_to_double) {
   const auto parsed = forge::codec::json::read_value(R"({"max":18446744073709551615})");
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.get_object()["max"].as_uint64() == 18446744073709551615ULL);

   const auto written = forge::codec::json::write_value(parsed.value);
   BOOST_REQUIRE(written.ok());
   BOOST_TEST(written.text.find("18446744073709551615") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(json_document_roundtrip_uses_config_document) {
   auto document = forge::config::core::document{};
   document.set("http.bind-host", "127.0.0.1");
   document.set("http.bind-port", 8080);
   document.set("http.tags", forge::config::core::value::array_type{forge::config::core::value{"alpha"},
                                                                    forge::config::core::value{"beta"}});

   const auto written = forge::codec::json::write_document(document, {.pretty = true});
   BOOST_REQUIRE(written.ok());
   const auto parsed = forge::codec::json::read_document(written.text);
   BOOST_REQUIRE(parsed.ok());
   BOOST_REQUIRE(parsed.value.try_get("http.bind-host") != nullptr);
   BOOST_REQUIRE(parsed.value.try_get("http.bind-port") != nullptr);
   BOOST_REQUIRE(parsed.value.try_get("http.tags") != nullptr);
}

BOOST_AUTO_TEST_CASE(json_typed_read_uses_schema_defaults_validation_and_unknown_policy) {
   const auto parsed = forge::codec::json::read<forge_json_tests::http_config>(
       R"({"bind-port":9090,"tls-enabled":false,"tags":["alpha"],"extra":1})");
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.bind_port == 9090U);
   BOOST_TEST(parsed.value.bind_host == "127.0.0.1");
   BOOST_REQUIRE_EQUAL(parsed.value.tags.size(), 1U);
   BOOST_TEST(parsed.diagnostics.size() == 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "json.unknown");

   auto options = forge::codec::json::read_options{};
   options.unknown_fields = forge::codec::json::unknown_field_policy::error;
   const auto rejected =
       forge::codec::json::read<forge_json_tests::http_config>(R"({"bind-port":9090,"extra":1})", options);
   BOOST_TEST(!rejected.ok());
   BOOST_TEST(rejected.diagnostics.front().code == "json.unknown");

   const auto invalid = forge::codec::json::read<forge_json_tests::http_config>(R"({"bind-port":0})");
   BOOST_TEST(!invalid.ok());
}

BOOST_AUTO_TEST_CASE(json_typed_load_uses_same_unknown_policy_as_read) {
   const auto path = std::filesystem::temp_directory_path() /
                     ("forge_json_unknown_policy_" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
   {
      auto out = std::ofstream{path};
      out << R"({"bind-port":9090,"extra":1})";
   }
   struct cleanup {
      std::filesystem::path path;
      ~cleanup() {
         std::error_code ignored;
         std::filesystem::remove(path, ignored);
      }
   } remove_file{path};

   const auto warned = forge::codec::json::load<forge_json_tests::http_config>(path);
   BOOST_REQUIRE(warned.ok());
   BOOST_REQUIRE_EQUAL(warned.diagnostics.size(), 1U);
   BOOST_TEST(warned.diagnostics.front().code == "json.unknown");

   auto rejected_options = forge::codec::json::read_options{};
   rejected_options.unknown_fields = forge::codec::json::unknown_field_policy::error;
   const auto rejected = forge::codec::json::load<forge_json_tests::http_config>(path, rejected_options);
   BOOST_TEST(!rejected.ok());
   BOOST_REQUIRE_EQUAL(rejected.diagnostics.size(), 1U);
   BOOST_TEST(rejected.diagnostics.front().code == "json.unknown");

   auto ignored_options = forge::codec::json::read_options{};
   ignored_options.unknown_fields = forge::codec::json::unknown_field_policy::ignore;
   const auto ignored = forge::codec::json::load<forge_json_tests::http_config>(path, ignored_options);
   BOOST_REQUIRE(ignored.ok());
   BOOST_TEST(ignored.diagnostics.empty());
   BOOST_TEST(ignored.value.bind_port == 9090U);
}

BOOST_AUTO_TEST_CASE(json_exact_described_records_validate_nested_fields_and_variants) {
   const auto options = forge::codec::json::read_options{
       .described_records = forge::codec::json::described_record_policy::exact,
   };
   const auto canonical = forge::codec::json::read<forge_json_tests::exact_record>(
       R"({"items":[{"value":1}],"choice":[0,{"value":2}]})", options);
   for (const auto& diagnostic : canonical.diagnostics) {
      BOOST_TEST_MESSAGE(diagnostic.code << " at " << diagnostic.path << ": " << diagnostic.message);
   }
   BOOST_REQUIRE(canonical.ok());
   BOOST_REQUIRE_EQUAL(canonical.value.items.size(), 1U);
   BOOST_TEST(canonical.value.items.front().value == 1U);
   BOOST_TEST(std::get<forge_json_tests::exact_leaf>(canonical.value.choice).value == 2U);
   BOOST_TEST(!canonical.value.optional.has_value());

   const auto written = forge::codec::json::write(canonical.value, {.pretty = true});
   BOOST_REQUIRE(written.ok());
   const auto written_roundtrip = forge::codec::json::read<forge_json_tests::exact_record>(written.text, options);
   BOOST_REQUIRE(written_roundtrip.ok());
   BOOST_CHECK(written_roundtrip.value == canonical.value);

   const auto path =
       std::filesystem::temp_directory_path() /
       ("forge_json_exact_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
   struct cleanup {
      std::filesystem::path path;
      ~cleanup() {
         auto ignored = std::error_code{};
         std::filesystem::remove(path, ignored);
      }
   } remove_file{path};
   const auto saved = forge::codec::json::save(path, canonical.value, {.pretty = true});
   BOOST_REQUIRE(saved.ok());
   const auto loaded = forge::codec::json::load<forge_json_tests::exact_record>(path, options);
   BOOST_REQUIRE(loaded.ok());
   BOOST_CHECK(loaded.value == canonical.value);

   const auto unknown = forge::codec::json::read<forge_json_tests::exact_record>(
       R"({"items":[{"value":1,"extra":2}],"choice":[0,{"value":2}]})", options);
   BOOST_REQUIRE(!unknown.ok());
   BOOST_TEST(unknown.diagnostics.front().code == "json.unknown");
   BOOST_TEST(unknown.diagnostics.front().path == "items[0].extra");

   const auto missing =
       forge::codec::json::read<forge_json_tests::exact_record>(R"({"items":[{}],"choice":[0,{"value":2}]})", options);
   BOOST_REQUIRE(!missing.ok());
   BOOST_TEST(missing.diagnostics.front().code == "json.missing");
   BOOST_TEST(missing.diagnostics.front().path == "items[0].value");

   const auto invalid_array =
       forge::codec::json::read<forge_json_tests::exact_record>(R"({"items":{},"choice":[0,{"value":2}]})", options);
   BOOST_REQUIRE(!invalid_array.ok());
   BOOST_TEST(invalid_array.diagnostics.front().code == "json.array");
   BOOST_TEST(invalid_array.diagnostics.front().path == "items");

   const auto object_variant =
       forge::codec::json::read<forge_json_tests::exact_record>(R"({"items":[],"choice":{"value":2}})", options);
   BOOST_REQUIRE(!object_variant.ok());
   BOOST_TEST(object_variant.diagnostics.front().code == "json.variant");
   BOOST_TEST(object_variant.diagnostics.front().path == "choice");

   const auto string_variant =
       forge::codec::json::read<forge_json_tests::exact_record>(R"({"items":[],"choice":"bad"})", options);
   BOOST_REQUIRE(!string_variant.ok());
   BOOST_TEST(string_variant.diagnostics.front().code == "json.variant");
   BOOST_TEST(string_variant.diagnostics.front().path == "choice");

   const auto invalid_index =
       forge::codec::json::read<forge_json_tests::exact_record>(R"({"items":[],"choice":[2,{"value":2}]})", options);
   BOOST_REQUIRE(!invalid_index.ok());
   BOOST_TEST(invalid_index.diagnostics.front().code == "json.variant");
   BOOST_TEST(invalid_index.diagnostics.front().path == "choice[0]");

   const auto negative_index =
       forge::codec::json::read<forge_json_tests::exact_record>(R"({"items":[],"choice":[-1,{"value":2}]})", options);
   BOOST_REQUIRE(!negative_index.ok());
   BOOST_TEST(negative_index.diagnostics.front().code == "json.variant");
   BOOST_TEST(negative_index.diagnostics.front().path == "choice[0]");

   const auto false_index = forge::codec::json::read<forge_json_tests::exact_record>(
       R"({"items":[],"choice":[false,{"value":2}]})", options);
   BOOST_REQUIRE(!false_index.ok());
   BOOST_TEST(false_index.diagnostics.front().code == "json.variant");
   BOOST_TEST(false_index.diagnostics.front().path == "choice[0]");

   const auto true_index =
       forge::codec::json::read<forge_json_tests::exact_record>(R"({"items":[],"choice":[true,{"value":2}]})", options);
   BOOST_REQUIRE(!true_index.ok());
   BOOST_TEST(true_index.diagnostics.front().code == "json.variant");
   BOOST_TEST(true_index.diagnostics.front().path == "choice[0]");

   const auto extra_variant_element = forge::codec::json::read<forge_json_tests::exact_record>(
       R"({"items":[],"choice":[0,{"value":2},false]})", options);
   BOOST_REQUIRE(!extra_variant_element.ok());
   BOOST_TEST(extra_variant_element.diagnostics.front().code == "json.variant");
   BOOST_TEST(extra_variant_element.diagnostics.front().path == "choice");

   const auto pointers = forge::codec::json::read<forge_json_tests::exact_pointer_record>(
       R"({"shared":{"bind-port":3},"unique":{"port":4}})", options);
   BOOST_REQUIRE(pointers.ok());
   BOOST_REQUIRE(pointers.value.shared);
   BOOST_REQUIRE(pointers.value.unique);
   BOOST_TEST(pointers.value.shared->bind_port == 3U);
   BOOST_TEST(pointers.value.unique->bind_port == 4U);

   const auto null_pointers =
       forge::codec::json::read<forge_json_tests::exact_pointer_record>(R"({"shared":null,"unique":null})", options);
   BOOST_REQUIRE(null_pointers.ok());
   BOOST_TEST(!null_pointers.value.shared);
   BOOST_TEST(!null_pointers.value.unique);

   const auto shared_missing = forge::codec::json::read<forge_json_tests::exact_pointer_record>(
       R"({"shared":{},"unique":{"bind-port":4}})", options);
   BOOST_REQUIRE(!shared_missing.ok());
   BOOST_TEST(shared_missing.diagnostics.front().code == "json.missing");
   BOOST_TEST(shared_missing.diagnostics.front().path == "shared.bind-port");

   const auto unique_unknown = forge::codec::json::read<forge_json_tests::exact_pointer_record>(
       R"({"shared":{"bind-port":3},"unique":{"bind-port":4,"extra":5}})", options);
   BOOST_REQUIRE(!unique_unknown.ok());
   BOOST_TEST(unique_unknown.diagnostics.front().code == "json.unknown");
   BOOST_TEST(unique_unknown.diagnostics.front().path == "unique.extra");

   const auto duplicate_alias = forge::codec::json::read<forge_json_tests::exact_pointer_record>(
       R"({"shared":{"bind-port":3,"port":4},"unique":{"bind-port":5}})", options);
   BOOST_REQUIRE(!duplicate_alias.ok());
   BOOST_TEST(duplicate_alias.diagnostics.front().code == "json.duplicate");
   BOOST_TEST(duplicate_alias.diagnostics.front().path == "shared.port");

   const auto schema_set = forge::codec::json::read<forge_json_tests::exact_schema_set_record>(
       R"({"values":[{"bind-port":1},{"port":2}]})", options);
   BOOST_REQUIRE(schema_set.ok());
   BOOST_REQUIRE_EQUAL(schema_set.value.values.size(), 2U);
   auto set_entry = schema_set.value.values.begin();
   BOOST_TEST(set_entry->bind_port == 1U);
   ++set_entry;
   BOOST_TEST(set_entry->bind_port == 2U);

   const auto schema_set_duplicate = forge::codec::json::read<forge_json_tests::exact_schema_set_record>(
       R"({"values":[{"bind-port":1},{"port":1}]})", options);
   BOOST_REQUIRE(!schema_set_duplicate.ok());
   BOOST_TEST(schema_set_duplicate.diagnostics.front().code == "json.duplicate");
   BOOST_TEST(schema_set_duplicate.diagnostics.front().path == "values[1]");
}

BOOST_AUTO_TEST_CASE(json_exact_described_records_validate_schema_names_and_associative_entries) {
   const auto options = forge::codec::json::read_options{
       .described_records = forge::codec::json::described_record_policy::exact,
   };

   const auto schema_record = forge::codec::json::read<forge_json_tests::http_config>(
       R"({"bind-port":9090,"bind-host":"127.0.0.1","tls-enabled":false,"tags":[]})", options);
   BOOST_REQUIRE(schema_record.ok());

   const auto schema_missing = forge::codec::json::read<forge_json_tests::http_config>(
       R"({"bind-port":9090,"bind-host":"127.0.0.1","tls-enabled":false})", options);
   BOOST_REQUIRE(!schema_missing.ok());
   BOOST_TEST(schema_missing.diagnostics.front().code == "json.missing");
   BOOST_TEST(schema_missing.diagnostics.front().path == "tags");

   const auto schema_unknown = forge::codec::json::read<forge_json_tests::http_config>(
       R"({"bind-port":9090,"bind-host":"127.0.0.1","tls-enabled":false,"tags":[],"extra":1})", options);
   BOOST_REQUIRE(!schema_unknown.ok());
   BOOST_TEST(schema_unknown.diagnostics.front().code == "json.unknown");
   BOOST_TEST(schema_unknown.diagnostics.front().path == "extra");

   const auto schema_duplicate = forge::codec::json::read<forge_json_tests::http_config>(
       R"({"bind-port":9090,"port":9091,"bind-host":"127.0.0.1","tls-enabled":false,"tags":[]})", options);
   BOOST_REQUIRE(!schema_duplicate.ok());
   BOOST_TEST(schema_duplicate.diagnostics.front().code == "json.duplicate");
   BOOST_TEST(schema_duplicate.diagnostics.front().path == "port");

   const auto duplicate_member = forge::codec::json::read<forge_json_tests::http_config>(
       R"({"bind-port":9090,"bind-port":9091,"bind-host":"127.0.0.1","tls-enabled":false,"tags":[]})", options);
   BOOST_REQUIRE(!duplicate_member.ok());
   BOOST_TEST(duplicate_member.diagnostics.front().code == "json.duplicate");
   BOOST_TEST(duplicate_member.diagnostics.front().path == "bind-port");

   const auto textual_scalars = forge::codec::json::read<forge_json_tests::http_config>(
       R"({"bind-port":"9090","bind-host":"127.0.0.1","tls-enabled":"false","tags":[]})", options);
   BOOST_REQUIRE(!textual_scalars.ok());
   BOOST_TEST(textual_scalars.diagnostics.front().code == "json.type");
   BOOST_TEST(textual_scalars.diagnostics.front().path == "bind-port");

   const auto escaped_duplicate =
       forge::codec::json::read<forge_json_tests::exact_leaf>(R"({"value":7,"\u0076alue":8})", options);
   BOOST_REQUIRE(!escaped_duplicate.ok());
   BOOST_TEST(escaped_duplicate.diagnostics.front().code == "json.duplicate");
   BOOST_TEST(escaped_duplicate.diagnostics.front().path == "value");

   const auto permissive_duplicate = forge::codec::json::read_value(R"({"value":7,"value":8})");
   BOOST_REQUIRE(permissive_duplicate.ok());

   const auto map_record =
       forge::codec::json::read<forge_json_tests::exact_map_record>(R"({"values":[["first",{"value":7}]]})", options);
   BOOST_REQUIRE(map_record.ok());
   BOOST_TEST(map_record.value.values.at("first").value == 7U);

   const auto map_missing_value =
       forge::codec::json::read<forge_json_tests::exact_map_record>(R"({"values":[["first"]]})", options);
   BOOST_REQUIRE(!map_missing_value.ok());
   BOOST_TEST(map_missing_value.diagnostics.front().code == "json.pair");
   BOOST_TEST(map_missing_value.diagnostics.front().path == "values[0]");

   const auto map_unknown = forge::codec::json::read<forge_json_tests::exact_map_record>(
       R"({"values":[["first",{"value":7,"extra":1}]]})", options);
   BOOST_REQUIRE(!map_unknown.ok());
   BOOST_TEST(map_unknown.diagnostics.front().code == "json.unknown");
   BOOST_TEST(map_unknown.diagnostics.front().path == "values[0][1].extra");

   const auto nested_duplicate = forge::codec::json::read<forge_json_tests::exact_map_record>(
       R"({"values":[["first",{"value":7,"value":8}]]})", options);
   BOOST_REQUIRE(!nested_duplicate.ok());
   BOOST_TEST(nested_duplicate.diagnostics.front().code == "json.duplicate");
   BOOST_TEST(nested_duplicate.diagnostics.front().path == "values[0][1].value");

   const auto map_duplicate = forge::codec::json::read<forge_json_tests::exact_map_record>(
       R"({"values":[["first",{"value":7}],["first",{"value":8}]]})", options);
   BOOST_REQUIRE(!map_duplicate.ok());
   BOOST_TEST(map_duplicate.diagnostics.front().code == "json.duplicate");
   BOOST_TEST(map_duplicate.diagnostics.front().path == "values[1][0]");

   const auto set_duplicate = forge::codec::json::read<forge_json_tests::exact_set_record>(
       R"({"ordered":["first","first"],"unordered":[]})", options);
   BOOST_REQUIRE(!set_duplicate.ok());
   BOOST_TEST(set_duplicate.diagnostics.front().code == "json.duplicate");
   BOOST_TEST(set_duplicate.diagnostics.front().path == "ordered[1]");

   const auto unordered_set_duplicate = forge::codec::json::read<forge_json_tests::exact_set_record>(
       R"({"ordered":[],"unordered":["first","first"]})", options);
   BOOST_REQUIRE(!unordered_set_duplicate.ok());
   BOOST_TEST(unordered_set_duplicate.diagnostics.front().code == "json.duplicate");
   BOOST_TEST(unordered_set_duplicate.diagnostics.front().path == "unordered[1]");

   const auto multi_index_record =
       forge::codec::json::read<forge_json_tests::exact_multi_index_record>(R"({"values":[{"value":7}]})", options);
   BOOST_REQUIRE(multi_index_record.ok());
   BOOST_REQUIRE_EQUAL(multi_index_record.value.values.size(), 1U);

   const auto multi_index_unknown = forge::codec::json::read<forge_json_tests::exact_multi_index_record>(
       R"({"values":[{"value":7,"extra":1}]})", options);
   BOOST_REQUIRE(!multi_index_unknown.ok());
   BOOST_TEST(multi_index_unknown.diagnostics.front().code == "json.unknown");
   BOOST_TEST(multi_index_unknown.diagnostics.front().path == "values[0].extra");

   const auto multi_index_duplicate = forge::codec::json::read<forge_json_tests::exact_multi_index_record>(
       R"({"values":[{"value":7},{"value":7}]})", options);
   BOOST_REQUIRE(!multi_index_duplicate.ok());
   BOOST_TEST(multi_index_duplicate.diagnostics.front().code == "json.duplicate");
   BOOST_TEST(multi_index_duplicate.diagnostics.front().path == "values[1]");

   const auto shorthand = forge::codec::json::read<forge_json_tests::tag_config>(R"({"tags":["alpha"]})", options);
   BOOST_REQUIRE(shorthand.ok());
   BOOST_REQUIRE_EQUAL(shorthand.value.tags.size(), 1U);
   BOOST_TEST(shorthand.value.tags.front().value == "alpha");
}

BOOST_AUTO_TEST_CASE(json_exact_described_records_validate_scalar_kinds_and_ranges) {
   const auto options = forge::codec::json::read_options{
       .described_records = forge::codec::json::described_record_policy::exact,
   };
   const auto canonical = forge::codec::json::read<forge_json_tests::exact_scalar_record>(
       R"({"enabled":true,"signed_value":-8,"unsigned_value":8,"ratio":1.5,"label":"ready"})", options);
   BOOST_REQUIRE(canonical.ok());
   BOOST_TEST(canonical.value.enabled);
   BOOST_TEST(canonical.value.signed_value == -8);
   BOOST_TEST(canonical.value.unsigned_value == 8U);
   BOOST_TEST(canonical.value.ratio == 1.5F);
   BOOST_TEST(canonical.value.label == "ready");

   const auto boolean_integer = forge::codec::json::read<forge_json_tests::exact_scalar_record>(
       R"({"enabled":true,"signed_value":false,"unsigned_value":8,"ratio":1.5,"label":"ready"})", options);
   BOOST_REQUIRE(!boolean_integer.ok());
   BOOST_TEST(boolean_integer.diagnostics.front().code == "json.type");
   BOOST_TEST(boolean_integer.diagnostics.front().path == "signed_value");

   const auto signed_overflow = forge::codec::json::read<forge_json_tests::exact_scalar_record>(
       R"({"enabled":true,"signed_value":-129,"unsigned_value":8,"ratio":1.5,"label":"ready"})", options);
   BOOST_REQUIRE(!signed_overflow.ok());
   BOOST_TEST(signed_overflow.diagnostics.front().code == "json.range");
   BOOST_TEST(signed_overflow.diagnostics.front().path == "signed_value");

   const auto unsigned_overflow = forge::codec::json::read<forge_json_tests::exact_scalar_record>(
       R"({"enabled":true,"signed_value":-8,"unsigned_value":256,"ratio":1.5,"label":"ready"})", options);
   BOOST_REQUIRE(!unsigned_overflow.ok());
   BOOST_TEST(unsigned_overflow.diagnostics.front().code == "json.range");
   BOOST_TEST(unsigned_overflow.diagnostics.front().path == "unsigned_value");

   const auto string_boolean = forge::codec::json::read<forge_json_tests::exact_scalar_record>(
       R"({"enabled":"true","signed_value":-8,"unsigned_value":8,"ratio":1.5,"label":"ready"})", options);
   BOOST_REQUIRE(!string_boolean.ok());
   BOOST_TEST(string_boolean.diagnostics.front().code == "json.type");
   BOOST_TEST(string_boolean.diagnostics.front().path == "enabled");

   const auto lossy_float_integer = forge::codec::json::read<forge_json_tests::exact_scalar_record>(
       R"({"enabled":true,"signed_value":-8,"unsigned_value":8,"ratio":16777217,"label":"ready"})", options);
   BOOST_REQUIRE(!lossy_float_integer.ok());
   BOOST_TEST(lossy_float_integer.diagnostics.front().code == "json.range");
   BOOST_TEST(lossy_float_integer.diagnostics.front().path == "ratio");

   const auto floating_underflow = forge::codec::json::read<forge_json_tests::exact_scalar_record>(
       R"({"enabled":true,"signed_value":-8,"unsigned_value":8,"ratio":1e-100,"label":"ready"})", options);
   BOOST_REQUIRE(!floating_underflow.ok());
   BOOST_TEST(floating_underflow.diagnostics.front().code == "json.range");
   BOOST_TEST(floating_underflow.diagnostics.front().path == "ratio");
}

BOOST_AUTO_TEST_CASE(json_exact_described_records_preserve_wide_integer_strings) {
   const auto options = forge::codec::json::read_options{
       .described_records = forge::codec::json::described_record_policy::exact,
   };

   constexpr auto unsigned_value = static_cast<unsigned __int128>(1) << 100;
   constexpr auto signed_value = -static_cast<__int128>(unsigned_value);
   const auto decoded = forge::codec::json::read<forge_json_tests::exact_wide_integer_record>(
       R"({"signed_value":"-1267650600228229401496703205376","unsigned_value":"1267650600228229401496703205376"})",
       options);

   BOOST_REQUIRE(decoded.ok());
   BOOST_CHECK(decoded.value.signed_value == signed_value);
   BOOST_CHECK(decoded.value.unsigned_value == unsigned_value);

   const auto overflow = forge::codec::json::read<forge_json_tests::exact_wide_integer_record>(
       R"({"signed_value":"0","unsigned_value":"340282366920938463463374607431768211456"})", options);
   BOOST_REQUIRE(!overflow.ok());
   BOOST_TEST(overflow.diagnostics.front().code == "json.range");
   BOOST_TEST(overflow.diagnostics.front().path == "unsigned_value");
}

BOOST_AUTO_TEST_CASE(json_exact_duplicate_scan_respects_max_depth) {
   const auto parsed = forge::codec::json::read_value(
       R"({"outer":{"inner":1}})",
       {.max_depth = 1, .described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(!parsed.ok());
   BOOST_TEST(parsed.diagnostics.front().code == "json.depth");
   BOOST_TEST(parsed.diagnostics.front().path == "outer.inner");
}

BOOST_AUTO_TEST_CASE(json_malformed_input_returns_forge_diagnostic) {
   const auto parsed = forge::codec::json::read_value(R"({"unterminated":)");
   BOOST_TEST(!parsed.ok());
   BOOST_REQUIRE_EQUAL(parsed.diagnostics.size(), 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "json.parse");
   BOOST_TEST(parsed.diagnostics.front().message.find("glz::") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(json_write_escapes_control_bytes_inside_strings) {
   const auto expected = std::string{"a\x01\b\0z", 5};
   const auto written =
       forge::codec::json::write_value(forge::variant{forge::mutable_variant_object{}("text", expected)});
   BOOST_REQUIRE(written.ok());
   BOOST_TEST(written.text.find("\\u0001") != std::string::npos);
   const auto escaped_backspace =
       written.text.find("\\b") != std::string::npos || written.text.find("\\u0008") != std::string::npos;
   BOOST_TEST(escaped_backspace);
   BOOST_TEST(written.text.find("\\u0000") != std::string::npos);
   BOOST_TEST(written.text.find('\0') == std::string::npos);

   const auto parsed = forge::codec::json::read_value(written.text);
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.get_object()["text"].get_string() == expected);
}

BOOST_AUTO_TEST_SUITE_END()
