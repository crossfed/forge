module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <typeindex>
#include <utility>

module forge.api.core.registry;

import forge.raw.raw;

namespace forge::api::core {
namespace {

void fail_stream_endpoints(
   const std::shared_ptr<detail::stream_endpoint>& input,
   const std::shared_ptr<detail::stream_endpoint>& output) noexcept {
   const auto error = std::make_exception_ptr(
      exceptions::protocol_error{"API stream dispatch failed"});
   if (input) {
      input->fail(error);
   }
   if (output) {
      output->fail(error);
   }
}

[[nodiscard]] frame make_response_base(const frame& request, frame_kind kind = frame_kind::response) {
   return frame{
       .kind = kind,
       .id = request.id,
       .api = request.api,
       .method = request.method,
       .meta = request.meta,
       .codec = request.codec,
   };
}

[[nodiscard]] frame make_error_response(const frame& request, error_payload payload) {
   auto response = make_response_base(request, frame_kind::error);
   forge::raw::pack(response.payload, payload);
   return response;
}

[[nodiscard]] frame make_protocol_error(const frame& request, std::string message, status status_code,
                                        exceptions::code code) {
   return make_error_response(request, error_payload{
                                           .error = "protocol_error",
                                           .message = std::move(message),
                                           .retryable = false,
                                           .status_code = status_code,
                                           .identity =
                                               {
                                                   .category = "forge.api",
                                                   .code = static_cast<std::uint32_t>(code),
                                               },
                                       });
}

[[nodiscard]] frame make_local_only_response(const frame& request) {
   return make_protocol_error(request, "API is local-only and cannot be invoked through a wire binding",
                              status::failed_precondition, exceptions::code::protocol_error);
}

[[nodiscard]] frame make_unavailable_response(const frame& request) {
   return make_protocol_error(request, "API is not available or version is incompatible", status::failed_precondition,
                              exceptions::code::incompatible_version);
}

[[nodiscard]] frame make_method_not_found_response(const frame& request) {
   return make_protocol_error(request, "API method is not available", status::not_found,
                              exceptions::code::method_not_found);
}

[[nodiscard]] frame project_failure(const frame& request, const method_descriptor& method, std::exception_ptr error) {
   try {
      if (error) {
         std::rethrow_exception(error);
      }
   } catch (const forge::exceptions::base& exception) {
      return make_error_response(request, project_error(method, exception));
   } catch (...) {
      return make_error_response(request, make_internal_error_payload());
   }
   return make_error_response(request, make_internal_error_payload());
}

[[nodiscard]] contextual_handler_gate
make_contextual_handler_gate(const method_descriptor& method, contextual_dispatch_hook before, frame& frame,
                             std::shared_ptr<bytes> response_request, std::shared_ptr<void> implementation,
                             std::shared_ptr<std::atomic_size_t> calls) {
   return [request_validator = method.request_validator,
           response_validator = method.response_validator,
           before = std::move(before),
           &frame,
           response_request = std::move(response_request),
           implementation = std::move(implementation),
           calls = std::move(calls)](request_view request,
                                     const canonical_request_encoder& encode) -> boost::asio::awaitable<std::shared_ptr<void>> {
      if (calls->fetch_add(1U, std::memory_order_relaxed) != 0U) {
         throw exceptions::protocol_error{"contextual API invoker acquired the handler gate more than once"};
      }
      if (before) {
         co_await before(frame, request, encode);
      }
      if (request_validator || response_validator) {
         // Existing byte validators observe the canonical typed request only after authorization succeeds.
         auto canonical_payload = encode();
         if (request_validator) {
            request_validator(canonical_payload);
         }
         if (response_validator) {
            *response_request = std::move(canonical_payload);
         }
      }
      co_return implementation;
   };
}

} // namespace

registry::registry() = default;
registry::~registry() = default;

const descriptor* registry::describe(api_ref requested) const noexcept {
   const auto* entry = find(std::move(requested));
   return entry == nullptr ? nullptr : &entry->descriptor;
}

boost::asio::awaitable<frame> registry::dispatch(frame request) const {
   co_return co_await dispatch_contextual(std::move(request), trusted_invocation{});
}

boost::asio::awaitable<frame>
registry::dispatch(frame request, trusted_invocation trusted) const {
   return dispatch_contextual(std::move(request), std::move(trusted));
}

boost::asio::awaitable<frame>
registry::dispatch_contextual(frame request, trusted_invocation trusted) const {
   return dispatch_contextual(std::move(request), std::move(trusted), {});
}

boost::asio::awaitable<frame>
registry::dispatch_contextual(frame request, trusted_invocation trusted, contextual_dispatch_hook before) const {
   if (request.kind != frame_kind::request) {
      co_return make_protocol_error(request, "API dispatch requires a request frame", status::invalid_argument,
                                    exceptions::code::protocol_error);
   }
   const auto* entry = find(request.api);
   if (entry == nullptr) {
      co_return make_unavailable_response(request);
   }
   if (!supports(entry->descriptor.supported_surfaces, surface::remote)) {
      co_return make_local_only_response(request);
   }

   const auto* method = find_method(entry->descriptor, request.method);
   if (method == nullptr || method->since_revision > request.api.min_revision || method->kind != method_kind::unary ||
       (!method->contextual_raw_invoker && (method->server_fields.active() || !method->raw_invoker))) {
      co_return make_method_not_found_response(request);
   }

   auto response = make_response_base(request);
   try {
      if (method->contextual_raw_invoker) {
         auto canonical_request = method->response_validator ? std::make_shared<bytes>() : std::shared_ptr<bytes>{};
         auto gate_calls = std::make_shared<std::atomic_size_t>(0U);
         auto gate = make_contextual_handler_gate(*method, std::move(before), request, canonical_request,
                                                  entry->implementation, gate_calls);
         response.payload = co_await method->contextual_raw_invoker(
            std::move(request.payload), std::move(trusted), std::move(gate));
         if (gate_calls->load(std::memory_order_relaxed) != 1U) {
            throw exceptions::protocol_error{"contextual API invoker must acquire the handler gate exactly once"};
         }
         if (method->response_validator) {
            method->response_validator(*canonical_request, response.payload);
         }
      } else {
         if (before) {
            const auto canonical_payload = [&request] { return request.payload; };
            co_await before(request, request_view{}, canonical_payload);
         }
         if (method->request_validator) {
            method->request_validator(request.payload);
         }
         auto request_payload = method->response_validator ? request.payload : bytes{};
         response.payload = co_await method->raw_invoker(entry->implementation, std::move(request.payload));
         if (method->response_validator) {
            method->response_validator(request_payload, response.payload);
         }
      }
      response.meta = request.meta;
      co_return response;
   } catch (...) {
      co_return project_failure(request, *method, std::current_exception());
   }
}

boost::asio::awaitable<frame> registry::dispatch_stream(frame request, std::shared_ptr<detail::stream_endpoint> input,
                                                        std::shared_ptr<detail::stream_endpoint> output) const {
   co_return co_await dispatch_stream_contextual(
      std::move(request), std::move(input), std::move(output), trusted_invocation{});
}

boost::asio::awaitable<frame>
registry::dispatch_stream(frame request, std::shared_ptr<detail::stream_endpoint> input,
                          std::shared_ptr<detail::stream_endpoint> output,
                          trusted_invocation trusted) const {
   return dispatch_stream_contextual(
      std::move(request), std::move(input), std::move(output), std::move(trusted));
}

boost::asio::awaitable<frame>
registry::dispatch_stream_contextual(frame request, std::shared_ptr<detail::stream_endpoint> input,
                                     std::shared_ptr<detail::stream_endpoint> output,
                                     trusted_invocation trusted) const {
   return dispatch_stream_contextual(
      std::move(request), std::move(input), std::move(output), std::move(trusted), {});
}

boost::asio::awaitable<frame>
registry::dispatch_stream_contextual(frame request, std::shared_ptr<detail::stream_endpoint> input,
                                     std::shared_ptr<detail::stream_endpoint> output,
                                     trusted_invocation trusted, contextual_dispatch_hook before) const {
   if (request.kind != frame_kind::request) {
      fail_stream_endpoints(input, output);
      co_return make_protocol_error(request, "API stream dispatch requires a request frame", status::invalid_argument,
                                    exceptions::code::protocol_error);
   }
   const auto* entry = find(request.api);
   if (entry == nullptr) {
      fail_stream_endpoints(input, output);
      co_return make_unavailable_response(request);
   }
   if (!supports(entry->descriptor.supported_surfaces, surface::remote)) {
      fail_stream_endpoints(input, output);
      co_return make_local_only_response(request);
   }

   const auto* method = find_method(entry->descriptor, request.method);
   if (method == nullptr || method->since_revision > request.api.min_revision || method->kind == method_kind::unary ||
       (!method->contextual_stream_invoker && (method->server_fields.active() || !method->stream_invoker))) {
      fail_stream_endpoints(input, output);
      co_return make_method_not_found_response(request);
   }

   const auto has_input = static_cast<bool>(input);
   const auto has_output = static_cast<bool>(output);
   const auto directions_match = (method->kind == method_kind::server_stream && !has_input && has_output) ||
                                 (method->kind == method_kind::client_stream && has_input && !has_output) ||
                                 (method->kind == method_kind::bidirectional_stream && has_input && has_output);
   if (!directions_match) {
      fail_stream_endpoints(input, output);
      co_return make_protocol_error(request, "API stream endpoints do not match method direction",
                                    status::invalid_argument, exceptions::code::protocol_error);
   }

   auto response = make_response_base(request);
   try {
      if (method->contextual_stream_invoker) {
         auto canonical_request = method->response_validator ? std::make_shared<bytes>() : std::shared_ptr<bytes>{};
         auto gate_calls = std::make_shared<std::atomic_size_t>(0U);
         auto gate = make_contextual_handler_gate(*method, std::move(before), request, canonical_request,
                                                  entry->implementation, gate_calls);
         response.payload = co_await method->contextual_stream_invoker(
            std::move(request.payload), input, output, std::move(trusted), std::move(gate));
         if (gate_calls->load(std::memory_order_relaxed) != 1U) {
            throw exceptions::protocol_error{"contextual API invoker must acquire the handler gate exactly once"};
         }
         if (method->response_validator) {
            method->response_validator(*canonical_request, response.payload);
         }
      } else {
         if (before) {
            const auto canonical_payload = [&request] { return request.payload; };
            co_await before(request, request_view{}, canonical_payload);
         }
         if (method->request_validator) {
            method->request_validator(request.payload);
         }
         auto request_payload = method->response_validator ? request.payload : bytes{};
         response.payload = co_await method->stream_invoker(entry->implementation, std::move(request.payload),
                                                              input, output);
         if (method->response_validator) {
            method->response_validator(request_payload, response.payload);
         }
      }
      response.meta = request.meta;
      co_return response;
   } catch (...) {
      fail_stream_endpoints(input, output);
      co_return project_failure(request, *method, std::current_exception());
   }
}

std::size_t registry::size() const noexcept {
   return entries_.size();
}

void registry::clear() noexcept {
   entries_.clear();
}

void registry::register_api(descriptor value, std::shared_ptr<void> implementation, std::type_index type) {
   if (!implementation) {
      throw exceptions::protocol_error{"cannot install null API implementation"};
   }
   if (!value.interface_type.hash_code() || value.interface_type != type) {
      value.interface_type = type;
   }
   const auto key = key_for(value.id.value, value.version.major);
   if (entries_.contains(key)) {
      throw exceptions::protocol_error{"duplicate API implementation"};
   }
   entries_.emplace(key, entry{std::move(value), std::move(implementation), type});
}

std::string registry::key_for(std::string_view id, std::uint16_t major) {
   std::ostringstream out;
   out << id << "/v" << major;
   return out.str();
}

const registry::entry* registry::find(api_ref requested) const noexcept {
   const auto iterator = entries_.find(key_for(requested.id.value, requested.major));
   if (iterator == entries_.end()) {
      return nullptr;
   }
   if (!compatible(iterator->second.descriptor, requested)) {
      return nullptr;
   }
   return &iterator->second;
}

} // namespace forge::api::core
