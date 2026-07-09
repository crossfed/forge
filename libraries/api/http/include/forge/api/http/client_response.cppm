module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <tuple>
#include <utility>
#include <vector>

export module forge.api.http.client_response;

import forge.api.core.connection;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.types;
import forge.net.http.body;
import forge.api.http.client_request;
import forge.api.http.parameters;
export import forge.net.http.client;
import forge.net.http.exceptions;
import forge.net.http.file;
export import forge.api.http.mapping;
import forge.net.http.stream;
import forge.net.http.types;
import forge.net.http.upload;
import forge.codec.json;
import forge.reflect.reflect;
import forge.codec.xml;

export namespace forge::api::http {

using namespace forge::net::http;

namespace detail {

[[nodiscard]] inline const forge::codec::xml::element* find_xml_child(const forge::codec::xml::element& parent,
                                                              std::string_view name) noexcept {
   const auto found = std::find_if(parent.children.begin(), parent.children.end(), [&](const forge::codec::xml::element& child) {
      return child.name == name;
   });
   return found == parent.children.end() ? nullptr : &*found;
}

[[nodiscard]] inline std::string xml_child_text(const forge::codec::xml::element& parent, std::string_view name) {
   if (const auto* child = find_xml_child(parent, name); child != nullptr) {
      return child->text;
   }
   return {};
}

[[nodiscard]] inline std::uint32_t parse_u32(std::string_view value, std::uint32_t fallback) {
   auto output = std::uint32_t{};
   const auto* begin = value.data();
   const auto* end = value.data() + value.size();
   const auto [position, error] = std::from_chars(begin, end, output);
   if (error == std::errc{} && position == end) {
      return output;
   }
   return fallback;
}

[[nodiscard]] inline forge::api::core::error_payload decode_xml_error_payload(const response& value) {
   auto decoded = forge::codec::xml::read_value(value.body(), forge::codec::xml::read_options{.source_name = "http.error"});
   if (!decoded.ok()) {
      return {};
   }
   const auto& root = decoded.value.root;
   const auto status_code = parse_u32(xml_child_text(root, "status_code"), value.result_int());
   auto identity = forge::api::core::error_identity{};
   if (const auto* identity_node = find_xml_child(root, "identity"); identity_node != nullptr) {
      identity.category = xml_child_text(*identity_node, "category");
      identity.code = parse_u32(xml_child_text(*identity_node, "code"), 0);
   }
   return forge::api::core::error_payload{
      .error = xml_child_text(root, "error"),
      .message = xml_child_text(root, "message"),
      .retryable = xml_child_text(root, "retryable") == "true" || xml_child_text(root, "retryable") == "1",
      .status_code = static_cast<forge::api::core::status>(status_code),
      .identity = std::move(identity),
   };
}

[[nodiscard]] inline forge::api::core::error_payload decode_error_payload(const response& value, error_codec codec) {
   switch (codec) {
   case error_codec::json: {
      auto decoded = forge::codec::json::read<forge::api::core::error_payload>(
         value.body(), forge::codec::json::read_options{.source_name = "http.error",
                                               .unknown_fields = forge::codec::json::unknown_field_policy::ignore});
      if (decoded.ok()) {
         auto payload = std::move(decoded.value);
         payload.status_code = static_cast<forge::api::core::status>(value.result_int());
         return payload;
      }
      break;
   }
   case error_codec::xml: {
      auto payload = decode_xml_error_payload(value);
      if (!payload.error.empty()) {
         if (payload.status_code == forge::api::core::status::internal) {
            payload.status_code = static_cast<forge::api::core::status>(value.result_int());
         }
         return payload;
      }
      break;
   }
   }
   return forge::api::core::error_payload{
      .error = "http_error",
      .message = value.body().empty() ? "HTTP API request failed" : value.body(),
      .retryable = false,
      .status_code = static_cast<forge::api::core::status>(value.result_int()),
      .identity =
         {
            .category = "forge.api",
            .code = static_cast<std::uint32_t>(forge::api::core::exceptions::code::remote_internal),
         },
   };
}

template <typename Response> [[nodiscard]] Response decode_response_body(const response& response_value,
                                                                         body_codec codec) {
   switch (codec) {
   case body_codec::json: {
      auto decoded = forge::codec::json::read<Response>(
         response_value.body(),
         forge::codec::json::read_options{.source_name = "http.response",
                                 .unknown_fields = forge::codec::json::unknown_field_policy::error});
      if (!decoded.ok()) {
         FORGE_THROW_EXCEPTION(forge::net::http::exceptions::bad_request, "HTTP API response JSON is invalid");
      }
      return std::move(decoded.value);
   }
   case body_codec::xml: {
      auto decoded = forge::codec::xml::read<Response>(
         response_value.body(),
         forge::codec::xml::read_options{.source_name = "http.response",
                                .unknown_fields = forge::codec::xml::unknown_field_policy::error});
      if (!decoded.ok()) {
         FORGE_THROW_EXCEPTION(forge::net::http::exceptions::bad_request, "HTTP API response XML is invalid");
      }
      return std::move(decoded.value);
   }
   }
   return Response{};
}

inline constexpr auto max_stream_error_body_bytes = std::uint64_t{64U * 1024U};

boost::asio::awaitable<std::string> read_bounded_error_body(body_reader& body) {
   auto output = std::string{};
   while (auto chunk = co_await body.async_read()) {
      if (chunk->bytes.size() > max_stream_error_body_bytes - output.size()) {
         FORGE_THROW_EXCEPTION(forge::net::http::exceptions::payload_too_large,
                             "HTTP API error response body exceeds the streaming client limit");
      }
      output.append(reinterpret_cast<const char*>(chunk->bytes.data()), chunk->bytes.size());
   }
   co_return output;
}

template <typename Request, typename Response>
boost::asio::awaitable<Response> call(client& target, const forge::api::core::descriptor& descriptor,
                                      const route& route, Request value) {
   if constexpr (detail::response_needs_stream_v<Response>) {
      auto request_value = make_client_request(target, route, value);
      auto body = bind_dto_request_body(request_value, route, value);

      auto response_value = body.has_value()
         ? co_await target.async_stream_request(std::move(request_value), std::move(*body))
         : co_await target.async_stream_request(std::move(request_value));
      if (response_value.head.result_int() < 200U || response_value.head.result_int() >= 300U) {
         response_value.head.body() = co_await read_bounded_error_body(response_value.body);
         auto error = decode_error_payload(response_value.head, route.error_body_codec);
         forge::api::core::raise_remote_error(error, forge::api::core::find_method(descriptor, route.method_name));
      }
      if constexpr (std::is_same_v<std::remove_cvref_t<Response>, file_response>) {
         co_return file_response::from_body(std::move(response_value.head), std::move(response_value.body));
      } else {
         co_return streaming_response::from_body(std::move(response_value.head), std::move(response_value.body));
      }
   } else if constexpr (detail::is_bytes_response_v<Response>) {
      auto request_value = make_client_request(target, route, value);
      auto body = bind_dto_request_body(request_value, route, value);
      auto response_value = body.has_value()
         ? co_await target.async_streaming_request(std::move(request_value), std::move(*body))
         : co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value, route.error_body_codec);
         forge::api::core::raise_remote_error(error, forge::api::core::find_method(descriptor, route.method_name));
      }
      auto bytes = std::vector<std::byte>(response_value.body().size());
      if (!bytes.empty()) {
         std::memcpy(bytes.data(), response_value.body().data(), response_value.body().size());
      }
      auto content_type = std::string{};
      if (auto iterator = response_value.find(field::content_type); iterator != response_value.end()) {
         content_type = std::string{iterator->value()};
      }
      co_return Response{
         .bytes = std::move(bytes),
         .content_type = content_type.empty() ? std::string{"application/octet-stream"} : std::move(content_type),
         .status_code = response_value.result(),
      };
   } else if constexpr (detail::is_empty_response_v<Response>) {
      auto request_value = make_client_request(target, route, value);
      auto body = bind_dto_request_body(request_value, route, value);
      auto response_value = body.has_value()
         ? co_await target.async_streaming_request(std::move(request_value), std::move(*body))
         : co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value, route.error_body_codec);
         forge::api::core::raise_remote_error(error, forge::api::core::find_method(descriptor, route.method_name));
      }
      co_return Response{.status_code = response_value.result()};
   } else {
      auto request_value = make_client_request(target, route, value);
      auto body = bind_dto_request_body(request_value, route, value);
      auto response_value = body.has_value()
         ? co_await target.async_streaming_request(std::move(request_value), std::move(*body))
         : co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value, route.error_body_codec);
         forge::api::core::raise_remote_error(error, forge::api::core::find_method(descriptor, route.method_name));
      }
      co_return decode_response_body<Response>(response_value, route.response_body_codec);
   }
}

template <typename Tuple, typename Response>
boost::asio::awaitable<Response> call_arguments(client& target,
                                                const forge::api::core::descriptor& descriptor,
                                                const route& route,
                                                Tuple value,
                                                const std::vector<std::string>& argument_names) {
   reject_http_positional_parameters(value);
   auto request_parts = make_client_request(target, route, value, argument_names);
   auto request_body = bind_positional_request_body(request_parts.value, route, value, request_parts.consumed);
   if constexpr (detail::response_needs_stream_v<Response>) {
      auto response_value = request_body.has_value()
         ? co_await target.async_stream_request(std::move(request_parts.value), std::move(*request_body))
         : co_await target.async_stream_request(std::move(request_parts.value));
      if (response_value.head.result_int() < 200U || response_value.head.result_int() >= 300U) {
         response_value.head.body() = co_await read_bounded_error_body(response_value.body);
         auto error = decode_error_payload(response_value.head, route.error_body_codec);
         forge::api::core::raise_remote_error(error, forge::api::core::find_method(descriptor, route.method_name));
      }
      if constexpr (std::is_same_v<std::remove_cvref_t<Response>, file_response>) {
         co_return file_response::from_body(std::move(response_value.head), std::move(response_value.body));
      } else {
         co_return streaming_response::from_body(std::move(response_value.head), std::move(response_value.body));
      }
   } else {
      auto response_value = request_body.has_value()
         ? co_await target.async_streaming_request(std::move(request_parts.value), std::move(*request_body))
         : co_await target.async_request(std::move(request_parts.value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value, route.error_body_codec);
         forge::api::core::raise_remote_error(error, forge::api::core::find_method(descriptor, route.method_name));
      }
      if constexpr (detail::is_bytes_response_v<Response>) {
         auto bytes = std::vector<std::byte>(response_value.body().size());
         if (!bytes.empty()) {
            std::memcpy(bytes.data(), response_value.body().data(), response_value.body().size());
         }
         auto content_type = std::string{};
         if (auto iterator = response_value.find(field::content_type); iterator != response_value.end()) {
            content_type = std::string{iterator->value()};
         }
         co_return Response{
            .bytes = std::move(bytes),
            .content_type = content_type.empty() ? std::string{"application/octet-stream"} : std::move(content_type),
            .status_code = response_value.result(),
         };
      } else if constexpr (detail::is_empty_response_v<Response>) {
         co_return Response{.status_code = response_value.result()};
      } else {
         co_return decode_response_body<Response>(response_value, route.response_body_codec);
      }
   }
}

} // namespace detail

} // namespace forge::api::http
