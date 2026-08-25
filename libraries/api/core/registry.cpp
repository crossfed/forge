module;

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
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

} // namespace

registry::registry() = default;
registry::~registry() = default;

const descriptor* registry::describe(api_ref requested) const {
   return pin(std::move(requested)).describe();
}

registry::snapshot registry::pin(api_ref requested) const {
   return snapshot{find(std::move(requested))};
}

boost::asio::awaitable<frame> registry::dispatch(frame request) const {
   auto selected = pin(request.api);
   return selected.dispatch_contextual(std::move(request), trusted_invocation{});
}

boost::asio::awaitable<frame>
registry::dispatch_contextual(frame request, trusted_invocation trusted) const {
   auto selected = pin(request.api);
   return selected.dispatch_contextual(std::move(request), std::move(trusted));
}

boost::asio::awaitable<frame>
registry::dispatch_contextual(frame request, trusted_invocation trusted, contextual_dispatch_hook before) const {
   auto selected = pin(request.api);
   return selected.dispatch_contextual(std::move(request), std::move(trusted), std::move(before));
}

boost::asio::awaitable<frame>
registry::snapshot::dispatch_contextual(frame request, trusted_invocation trusted) const {
   return dispatch_contextual_entry(entry_, std::move(request), std::move(trusted), {});
}

boost::asio::awaitable<frame>
registry::snapshot::dispatch_contextual(frame request, trusted_invocation trusted,
                                        contextual_dispatch_hook before) const {
   return dispatch_contextual_entry(entry_, std::move(request), std::move(trusted), std::move(before));
}

boost::asio::awaitable<frame>
registry::snapshot::dispatch_contextual_entry(std::shared_ptr<const registry::entry> entry, frame request,
                                              trusted_invocation trusted, contextual_dispatch_hook before) {
   if (request.kind != frame_kind::request) {
      co_return make_protocol_error(request, "API dispatch requires a request frame", status::invalid_argument,
                                    exceptions::code::protocol_error);
   }
   if (entry == nullptr || !compatible(entry->descriptor, request.api)) {
      co_return make_unavailable_response(request);
   }
   if (!supports(entry->descriptor.supported_surfaces, surface::remote)) {
      co_return make_local_only_response(request);
   }

   const auto* method = find_method(entry->descriptor, request.method);
   const auto* contextual = method == nullptr ? nullptr : detail::contextual_unary_for(*method);
   const auto* fields = method == nullptr ? nullptr : detail::server_fields_for(*method);
   if (method == nullptr || method->since_revision > request.api.min_revision || method->kind != method_kind::unary ||
       ((contextual == nullptr || !*contextual) &&
        ((fields != nullptr && fields->active()) || !method->raw_invoker))) {
      co_return make_method_not_found_response(request);
   }

   auto response = make_response_base(request);
   auto gate_control = std::optional<detail::contextual_handler_gate_control>{};
   try {
      if (contextual != nullptr && *contextual) {
         auto payload = std::move(request.payload);
         gate_control.emplace(
            make_response_base(request, request.kind), std::move(before),
            method->request_validator, method->response_validator,
            entry->implementation);
         auto gate = gate_control->take_gate();
         response.payload = co_await (*contextual)(
            std::move(payload), std::move(trusted), std::move(gate));
         gate_control->close();
         if (!gate_control->completed_successfully_once()) {
            throw exceptions::protocol_error{"contextual API invoker must acquire the handler gate exactly once"};
         }
         gate_control->validate_response(response.payload);
         gate_control->transfer_completion_metadata(response);
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
      if (!gate_control) {
         response.meta = request.meta;
      }
      co_return response;
   } catch (...) {
      if (gate_control) {
         gate_control->close();
         gate_control->transfer_completion_metadata(response);
         co_return project_failure(response, *method, std::current_exception());
      }
      co_return project_failure(request, *method, std::current_exception());
   }
}

boost::asio::awaitable<frame> registry::dispatch_stream(frame request, std::shared_ptr<detail::stream_endpoint> input,
                                                        std::shared_ptr<detail::stream_endpoint> output) const {
   auto selected = pin(request.api);
   return selected.dispatch_stream_contextual(
      std::move(request), std::move(input), std::move(output), trusted_invocation{});
}

boost::asio::awaitable<frame>
registry::dispatch_stream_contextual(frame request, std::shared_ptr<detail::stream_endpoint> input,
                                     std::shared_ptr<detail::stream_endpoint> output,
                                     trusted_invocation trusted) const {
   auto selected = pin(request.api);
   return selected.dispatch_stream_contextual(
      std::move(request), std::move(input), std::move(output), std::move(trusted), {});
}

boost::asio::awaitable<frame>
registry::dispatch_stream_contextual(frame request, std::shared_ptr<detail::stream_endpoint> input,
                                     std::shared_ptr<detail::stream_endpoint> output,
                                     trusted_invocation trusted, contextual_dispatch_hook before) const {
   auto selected = pin(request.api);
   return selected.dispatch_stream_contextual(
      std::move(request), std::move(input), std::move(output), std::move(trusted), std::move(before));
}

boost::asio::awaitable<frame>
registry::snapshot::dispatch_stream_contextual(frame request, std::shared_ptr<detail::stream_endpoint> input,
                                               std::shared_ptr<detail::stream_endpoint> output,
                                               trusted_invocation trusted) const {
   return dispatch_stream_contextual_entry(
      entry_, std::move(request), std::move(input), std::move(output), std::move(trusted), {});
}

boost::asio::awaitable<frame>
registry::snapshot::dispatch_stream_contextual(frame request, std::shared_ptr<detail::stream_endpoint> input,
                                               std::shared_ptr<detail::stream_endpoint> output,
                                               trusted_invocation trusted, contextual_dispatch_hook before) const {
   return dispatch_stream_contextual_entry(
      entry_, std::move(request), std::move(input), std::move(output), std::move(trusted), std::move(before));
}

boost::asio::awaitable<frame>
registry::snapshot::dispatch_stream_contextual_entry(std::shared_ptr<const registry::entry> entry, frame request,
                                                     std::shared_ptr<detail::stream_endpoint> input,
                                                     std::shared_ptr<detail::stream_endpoint> output,
                                                     trusted_invocation trusted, contextual_dispatch_hook before) {
   if (request.kind != frame_kind::request) {
      fail_stream_endpoints(input, output);
      co_return make_protocol_error(request, "API stream dispatch requires a request frame", status::invalid_argument,
                                    exceptions::code::protocol_error);
   }
   if (entry == nullptr || !compatible(entry->descriptor, request.api)) {
      fail_stream_endpoints(input, output);
      co_return make_unavailable_response(request);
   }
   if (!supports(entry->descriptor.supported_surfaces, surface::remote)) {
      fail_stream_endpoints(input, output);
      co_return make_local_only_response(request);
   }

   const auto* method = find_method(entry->descriptor, request.method);
   const auto* contextual = method == nullptr ? nullptr : detail::contextual_stream_for(*method);
   const auto* fields = method == nullptr ? nullptr : detail::server_fields_for(*method);
   if (method == nullptr || method->since_revision > request.api.min_revision || method->kind == method_kind::unary ||
       ((contextual == nullptr || !*contextual) &&
        ((fields != nullptr && fields->active()) || !method->stream_invoker))) {
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
   auto gate_control = std::optional<detail::contextual_handler_gate_control>{};
   try {
      if (contextual != nullptr && *contextual) {
         auto payload = std::move(request.payload);
         gate_control.emplace(
            make_response_base(request, request.kind), std::move(before),
            method->request_validator, method->response_validator,
            entry->implementation);
         auto gate = gate_control->take_gate();
         response.payload = co_await (*contextual)(
            std::move(payload), input, output, std::move(trusted), std::move(gate));
         gate_control->close();
         if (!gate_control->completed_successfully_once()) {
            throw exceptions::protocol_error{"contextual API invoker must acquire the handler gate exactly once"};
         }
         gate_control->validate_response(response.payload);
         gate_control->transfer_completion_metadata(response);
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
      if (!gate_control) {
         response.meta = request.meta;
      }
      co_return response;
   } catch (...) {
      if (gate_control) {
         gate_control->close();
      }
      fail_stream_endpoints(input, output);
      if (gate_control) {
         gate_control->transfer_completion_metadata(response);
         co_return project_failure(response, *method, std::current_exception());
      }
      co_return project_failure(request, *method, std::current_exception());
   }
}

std::size_t registry::size() const {
   std::scoped_lock lock{entries_mutex_};
   return entries_.size();
}

void registry::clear() {
   decltype(entries_) removed;
   {
      std::scoped_lock lock{entries_mutex_};
      removed.swap(entries_);
   }
}

void registry::register_api(descriptor value, std::shared_ptr<void> implementation, std::type_index type) {
   if (!implementation) {
      throw exceptions::protocol_error{"cannot install null API implementation"};
   }
   if (!value.interface_type.hash_code() || value.interface_type != type) {
      value.interface_type = type;
   }
   const auto key = key_for(value.id.value, value.version.major);
   auto entry = std::make_shared<const registry::entry>(
      registry::entry{std::move(value), std::move(implementation), type});
   std::scoped_lock lock{entries_mutex_};
   if (entries_.contains(key)) {
      throw exceptions::protocol_error{"duplicate API implementation"};
   }
   entries_.emplace(key, std::move(entry));
}

std::string registry::key_for(std::string_view id, std::uint16_t major) {
   std::ostringstream out;
   out << id << "/v" << major;
   return out.str();
}

std::shared_ptr<const registry::entry> registry::find(api_ref requested) const {
   const auto key = key_for(requested.id.value, requested.major);
   std::scoped_lock lock{entries_mutex_};
   const auto iterator = entries_.find(key);
   if (iterator == entries_.end()) {
      return {};
   }
   if (!compatible(iterator->second->descriptor, requested)) {
      return {};
   }
   return iterator->second;
}

} // namespace forge::api::core
