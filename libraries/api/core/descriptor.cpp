module;

#include <boost/asio/awaitable.hpp>

#include <functional>
#include <string_view>
#include <string>

#include <utility>

module forge.api.core.descriptor;

#include "details/contextual_handler_gate_state.hxx"

namespace forge::api::core {

contextual_handler_gate::contextual_handler_gate(
   std::weak_ptr<detail::contextual_handler_gate_state> state)
   : state_{std::move(state)} {}

contextual_handler_gate::contextual_handler_gate(contextual_handler_gate&& other) noexcept
   : state_{std::move(other.state_)}, acquired_{std::exchange(other.acquired_, true)} {}

contextual_handler_gate&
contextual_handler_gate::operator=(contextual_handler_gate&& other) noexcept {
   if (this != &other) {
      state_ = std::move(other.state_);
      acquired_ = std::exchange(other.acquired_, true);
   }
   return *this;
}

contextual_handler_gate::~contextual_handler_gate() = default;

boost::asio::awaitable<std::shared_ptr<void>>
contextual_handler_gate::operator()(request_view request, const canonical_request_encoder& encode) {
   auto state = state_.lock();
   if (!state) {
      throw exceptions::protocol_error{"contextual API handler gate is no longer active"};
   }
   if (acquired_) {
      state->reject_reacquire();
      throw exceptions::protocol_error{"contextual API invoker acquired the handler gate more than once"};
   }
   acquired_ = true;
   co_return co_await state->acquire(request, encode);
}

detail::contextual_handler_gate_control::contextual_handler_gate_control(
   frame request, contextual_dispatch_hook before,
   std::function<void(const bytes&)> request_validator,
   std::function<void(const bytes&, const bytes&)> response_validator,
   std::shared_ptr<void> implementation)
   : state_{std::make_shared<contextual_handler_gate_state>(
        std::move(request), std::move(before), std::move(request_validator),
        std::move(response_validator), std::move(implementation))} {}

detail::contextual_handler_gate_control::contextual_handler_gate_control(
   contextual_handler_gate_control&& other) noexcept
   : state_{std::move(other.state_)}, gate_taken_{std::exchange(other.gate_taken_, true)} {}

detail::contextual_handler_gate_control&
detail::contextual_handler_gate_control::operator=(contextual_handler_gate_control&& other) noexcept {
   if (this != &other) {
      close();
      state_ = std::move(other.state_);
      gate_taken_ = std::exchange(other.gate_taken_, true);
   }
   return *this;
}

detail::contextual_handler_gate_control::~contextual_handler_gate_control() noexcept {
   close();
}

contextual_handler_gate detail::contextual_handler_gate_control::take_gate() {
   if (!state_ || gate_taken_) {
      throw exceptions::protocol_error{"contextual API handler gate is no longer active"};
   }
   gate_taken_ = true;
   return contextual_handler_gate_state::make_gate(state_);
}

void detail::contextual_handler_gate_control::close() noexcept {
   if (state_) {
      state_->close();
   }
}

bool detail::contextual_handler_gate_control::completed_successfully_once() const {
   return state_ && state_->completed_successfully_once();
}

void detail::contextual_handler_gate_control::validate_response(const bytes& response) const {
   if (!state_) {
      throw exceptions::protocol_error{"contextual API handler gate is no longer active"};
   }
   state_->validate_response(response);
}

void detail::contextual_handler_gate_control::transfer_completion_metadata(frame& response) {
   if (state_) {
      state_->transfer_completion_metadata(response.meta);
   }
}

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
