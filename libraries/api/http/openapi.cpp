module;

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

module forge.api.http.openapi;

import forge.net.http.types;

namespace forge::api::http::detail {
namespace {

[[nodiscard]] std::string verb_name(forge::net::http::method verb) {
   using forge::net::http::method;
   switch (verb) {
   case method::get:
      return "get";
   case method::head:
      return "head";
   case method::post:
      return "post";
   case method::put:
      return "put";
   case method::patch:
      return "patch";
   case method::delete_:
      return "delete";
   default:
      return "post";
   }
}

[[nodiscard]] std::string status_name(forge::net::http::status value) {
   return std::to_string(static_cast<unsigned>(value));
}

struct target_description {
   std::string path;
   std::vector<std::string> path_fields;
   std::vector<field_binding> query;
};

[[nodiscard]] target_description describe_target(std::string_view target) {
   auto output = target_description{};
   const auto question = target.find('?');
   const auto path = target.substr(0, question);
   output.path.reserve(path.size());
   for (auto position = std::size_t{}; position < path.size();) {
      if (path[position] == ':' && (position == 0U || path[position - 1U] == '/')) {
         const auto end = path.find('/', position + 1U);
         const auto size = (end == std::string_view::npos ? path.size() : end) - position - 1U;
         auto name = std::string{path.substr(position + 1U, size)};
         output.path_fields.push_back(name);
         output.path.push_back('{');
         output.path += name;
         output.path.push_back('}');
         position += size + 1U;
         continue;
      }
      if (path[position] != '{') {
         output.path.push_back(path[position++]);
         continue;
      }
      const auto close = path.find('}', position + 1U);
      if (close == std::string_view::npos) {
         output.path.append(path.substr(position));
         break;
      }
      auto name = std::string{path.substr(position + 1U, close - position - 1U)};
      output.path_fields.push_back(name);
      output.path.push_back('{');
      output.path += name;
      output.path.push_back('}');
      position = close + 1U;
   }

   if (question == std::string_view::npos) {
      return output;
   }
   auto query = target.substr(question + 1U);
   while (!query.empty()) {
      const auto separator = query.find('&');
      const auto entry = query.substr(0, separator);
      const auto equals = entry.find('=');
      if (equals != std::string_view::npos) {
         auto name = std::string{entry.substr(0, equals)};
         auto field = std::string{entry.substr(equals + 1U)};
         if (field.size() >= 2U && field.front() == '{' && field.back() == '}') {
            field = field.substr(1U, field.size() - 2U);
         }
         output.query.push_back(field_binding{.field = std::move(field), .name = std::move(name)});
      }
      if (separator == std::string_view::npos) {
         break;
      }
      query.remove_prefix(separator + 1U);
   }
   return output;
}

[[nodiscard]] const openapi_field* find_field(const openapi_operation& operation, std::string_view name) {
   const auto iterator = std::ranges::find(operation.request_fields, name, &openapi_field::name);
   return iterator == operation.request_fields.end() ? nullptr : &*iterator;
}

[[nodiscard]] forge::variant parameter(const openapi_operation& operation, std::string name, std::string location,
                                       bool force_required) {
   const auto* field = find_field(operation, name);
   auto value = forge::mutable_variant_object{}("name", name)("in", std::move(location))(
       "required", force_required || (field != nullptr && field->required));
   value("schema", field == nullptr ? unconstrained_schema("unknown request field") : field->schema);
   return forge::variant{std::move(value)};
}

[[nodiscard]] forge::variant media_schema(const forge::variant& schema) {
   return forge::variant{
       forge::mutable_variant_object{}("application/json", forge::mutable_variant_object{}("schema", schema))};
}

[[nodiscard]] forge::variant declared_error_document(const forge::api::core::error_descriptor& error) {
   return forge::variant{forge::mutable_variant_object{}("name", error.name)(
       "status_code", static_cast<std::uint64_t>(error.status_code))("retryable", error.retryable)(
       "identity", forge::mutable_variant_object{}("category", error.identity.category)(
                       "code", static_cast<std::uint64_t>(error.identity.code)))};
}

[[nodiscard]] forge::variant error_response_schema(const forge::api::core::method_descriptor* method) {
   auto schema = forge::mutable_variant_object{make_json_schema<forge::api::core::error_payload>()};
   if (method != nullptr && !method->errors.empty()) {
      auto errors = forge::variants{};
      errors.reserve(method->errors.size());
      for (const auto& error : method->errors) {
         errors.push_back(declared_error_document(error));
      }
      schema("x-forge-declared-errors", std::move(errors));
   }
   return forge::variant{std::move(schema)};
}

[[nodiscard]] forge::variant operation_document(const forge::api::core::descriptor& api,
                                                const openapi_operation& operation) {
   auto value = forge::mutable_variant_object{}("operationId", api.id.value + "." + operation.mapping.method_name);
   auto parameters = forge::variants{};
   const auto target = describe_target(operation.mapping.target);
   for (const auto& name : target.path_fields) {
      parameters.push_back(parameter(operation, name, "path", true));
   }
   for (const auto& entry : target.query) {
      parameters.push_back(parameter(operation, entry.field, "query", false));
   }
   for (const auto& entry : operation.mapping.headers) {
      parameters.push_back(parameter(operation, entry.field, "header", false));
   }
   if (!parameters.empty()) {
      value("parameters", std::move(parameters));
   }

   if (uses_request_body(operation.mapping.verb) && !operation.mapping.body_stream_field.has_value() &&
       operation.mapping.forms.empty()) {
      value("requestBody",
            forge::mutable_variant_object{}("required", true)("content", media_schema(operation.request_schema)));
   }

   auto response = forge::mutable_variant_object{}("description", "Successful response");
   if (!operation.mapping.response_file && !operation.mapping.response_stream) {
      response("content", media_schema(operation.response_schema));
   }
   auto responses = forge::mutable_variant_object{};
   responses.set(status_name(operation.mapping.success_status), forge::variant{std::move(response)});
   const auto* method = forge::api::core::find_method(api, operation.mapping.method_name);
   responses.set("default", forge::variant{forge::mutable_variant_object{}("description", "Forge API error")(
                                "content", media_schema(error_response_schema(method)))});
   value("responses", std::move(responses));
   if (operation.mapping.cache == cache_policy::no_store) {
      value("x-forge-cache-policy", "no-store");
   }
   return forge::variant{std::move(value)};
}

} // namespace

forge::variant build_openapi_document(const forge::api::core::descriptor& api,
                                      std::vector<openapi_operation> operations, openapi_info info) {
   if (info.title.empty()) {
      info.title = api.id.value;
   }
   if (info.version.empty()) {
      info.version = std::to_string(api.version.major) + "." + std::to_string(api.version.revision);
   }

   auto paths = forge::mutable_variant_object{};
   for (const auto& operation : operations) {
      const auto target = describe_target(operation.mapping.target);
      auto path = paths.find(target.path);
      auto methods =
          path == paths.end() ? forge::mutable_variant_object{} : forge::mutable_variant_object{path->value()};
      methods.set(verb_name(operation.mapping.verb), operation_document(api, operation));
      paths.set(target.path, forge::variant{std::move(methods)});
   }

   auto metadata = forge::mutable_variant_object{}("title", std::move(info.title))("version", std::move(info.version));
   if (!info.description.empty()) {
      metadata("description", std::move(info.description));
   }
   auto document =
       forge::mutable_variant_object{}("openapi", "3.1.0")("info", std::move(metadata))("paths", std::move(paths));
   if (!info.servers.empty()) {
      auto servers = forge::variants{};
      servers.reserve(info.servers.size());
      for (auto& url : info.servers) {
         servers.emplace_back(forge::mutable_variant_object{}("url", std::move(url)));
      }
      document("servers", std::move(servers));
   }
   return forge::variant{std::move(document)};
}

} // namespace forge::api::http::detail
