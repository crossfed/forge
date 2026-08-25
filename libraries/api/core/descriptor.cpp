module;

#include <boost/asio/awaitable.hpp>

#include <functional>
#include <optional>
#include <string_view>
#include <string>

#include <utility>

module forge.api.core.descriptor;

namespace forge::api::core {

bytes detail::contextual_request_encoder::operator()(const void* request) const {
   return encode(request);
}

boost::asio::awaitable<bytes>
detail::contextual_unary_invoker::operator()(std::shared_ptr<void> implementation,
                                             bytes payload) const {
   return invoke(std::move(implementation), std::move(payload));
}

boost::asio::awaitable<bytes>
detail::contextual_stream_invoker::operator()(
   std::shared_ptr<void> implementation, bytes payload,
   std::shared_ptr<stream_endpoint> input,
   std::shared_ptr<stream_endpoint> output) const {
   return invoke(std::move(implementation), std::move(payload),
                 std::move(input), std::move(output));
}

void detail::install_request_context(method_descriptor& method,
                                     server_field_operations fields,
                                     std::function<bytes(void*)> encode_owned) {
   auto encode = std::move(method.request_encoder);
   if (const auto* contextual = encode.target<contextual_request_encoder>()) {
      encode = contextual->encode;
   }
   method.request_encoder = contextual_request_encoder{
      .encode = std::move(encode),
      .encode_owned = std::move(encode_owned),
      .fields = std::move(fields),
   };
}

void detail::install_unary_context(method_descriptor& method,
                                   server_field_operations fields,
                                   contextual_raw_invoker invoke) {
   auto legacy = std::move(method.raw_invoker);
   if (const auto* contextual = legacy.target<contextual_unary_invoker>()) {
      legacy = contextual->invoke;
   }
   method.raw_invoker = contextual_unary_invoker{
      .invoke = std::move(legacy),
      .invoke_contextual = std::move(invoke),
      .fields = std::move(fields),
   };
}

void detail::install_stream_context(method_descriptor& method,
                                    server_field_operations fields,
                                    contextual_raw_stream_invoker invoke) {
   auto legacy = std::move(method.stream_invoker);
   if (const auto* contextual = legacy.target<contextual_stream_invoker>()) {
      legacy = contextual->invoke;
   }
   method.stream_invoker = contextual_stream_invoker{
      .invoke = std::move(legacy),
      .invoke_contextual = std::move(invoke),
      .fields = std::move(fields),
   };
}

void detail::remove_request_context(method_descriptor& method) {
   const auto* contextual =
      method.request_encoder.target<contextual_request_encoder>();
   if (contextual != nullptr) {
      auto encode = contextual->encode;
      method.request_encoder = std::move(encode);
   }
}

void detail::remove_unary_context(method_descriptor& method) {
   const auto* contextual =
      method.raw_invoker.target<contextual_unary_invoker>();
   if (contextual != nullptr) {
      auto invoke = contextual->invoke;
      method.raw_invoker = std::move(invoke);
   }
}

void detail::remove_stream_context(method_descriptor& method) {
   const auto* contextual =
      method.stream_invoker.target<contextual_stream_invoker>();
   if (contextual != nullptr) {
      auto invoke = contextual->invoke;
      method.stream_invoker = std::move(invoke);
   }
}

const detail::server_field_operations*
detail::server_fields_for(const method_descriptor& method) noexcept {
   if (const auto* contextual =
          method.raw_invoker.target<contextual_unary_invoker>()) {
      return &contextual->fields;
   }
   if (const auto* contextual =
          method.stream_invoker.target<contextual_stream_invoker>()) {
      return &contextual->fields;
   }
   if (const auto* contextual =
          method.request_encoder.target<contextual_request_encoder>()) {
      return &contextual->fields;
   }
   return nullptr;
}

const contextual_raw_invoker*
detail::contextual_unary_for(const method_descriptor& method) noexcept {
   const auto* contextual =
      method.raw_invoker.target<contextual_unary_invoker>();
   return contextual == nullptr ? nullptr : &contextual->invoke_contextual;
}

const contextual_raw_stream_invoker*
detail::contextual_stream_for(const method_descriptor& method) noexcept {
   const auto* contextual =
      method.stream_invoker.target<contextual_stream_invoker>();
   return contextual == nullptr ? nullptr : &contextual->invoke_contextual;
}

std::optional<bytes>
detail::encode_owned_request(const method_descriptor& method, void* request) {
   const auto* contextual =
      method.request_encoder.target<contextual_request_encoder>();
   if (contextual == nullptr || !contextual->encode_owned) {
      return std::nullopt;
   }
   return contextual->encode_owned(request);
}

void detail::reset_fixed_request(const method_descriptor& method, void* request) {
   const auto* fields = server_fields_for(method);
   if (fields != nullptr && fields->reset_fixed) {
      fields->reset_fixed(request);
   }
}

void detail::apply_wire_request(const method_descriptor& method, void* request,
                                const trusted_invocation& trusted) {
   const auto* fields = server_fields_for(method);
   if (fields == nullptr) {
      return;
   }
   if (fields->reset_wire) {
      fields->reset_wire(request);
   }
   if (fields->apply_wire) {
      fields->apply_wire(request, trusted);
   }
}

void detail::apply_fixed_request(const method_descriptor& method, void* request,
                                 const trusted_invocation& trusted) {
   const auto* fields = server_fields_for(method);
   if (fields == nullptr) {
      return;
   }
   if (fields->reset_fixed) {
      fields->reset_fixed(request);
   }
   if (fields->apply_fixed) {
      fields->apply_fixed(request, trusted);
   }
}

bool compatible(const descriptor& available, const api_ref& requested) noexcept {
   return available.id == requested.id && available.version.major == requested.major &&
          available.version.revision >= requested.min_revision;
}

bool compatible(const method_descriptor& available, const method_descriptor& requested) noexcept {
   return available.name == requested.name && available.kind == requested.kind;
}

const method_descriptor* find_method(const descriptor& api, std::string_view name) noexcept {
   for (const auto& method : api.methods) {
      if (method.name == name) {
         return &method;
      }
   }
   return nullptr;
}

} // namespace forge::api
