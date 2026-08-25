module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.api.core.binding;

namespace forge::api::core {
namespace {

void fail_stream_endpoints(
   const std::shared_ptr<detail::stream_endpoint>& input,
   const std::shared_ptr<detail::stream_endpoint>& output) noexcept {
   const auto error = std::make_exception_ptr(
      exceptions::protocol_error{"API stream binding failed"});
   if (input) {
      input->fail(error);
   }
   if (output) {
      output->fail(error);
   }
}

[[nodiscard]] call_context make_context(frame& value) {
   return call_context{
      .id = value.id,
      .api = value.api,
      .method = value.method,
      .meta = std::move(value.meta),
      .payload = std::move(value.payload),
      .codec = value.codec,
      .kind = value.kind,
   };
}

[[nodiscard]] frame make_api_not_exported_response(const frame& request) {
   auto response = frame{
      .kind = frame_kind::error,
      .id = request.id,
      .api = request.api,
      .method = request.method,
      .meta = request.meta,
      .codec = request.codec,
   };
   forge::raw::pack(
      response.payload,
      error_payload{
         .error = "api_not_exported",
         .message = "API is not exported by this binding plan",
         .retryable = false,
         .status_code = status::permission_denied,
         .identity = {
            .category = "forge.api",
            .code = static_cast<std::uint32_t>(
               exceptions::code::incompatible_version),
         },
      });
   return response;
}

[[nodiscard]] const descriptor*
find_export(const std::vector<descriptor>& exports,
            const api_ref& requested) noexcept {
   for (const auto& available : exports) {
      if (compatible(available, requested)) {
         return &available;
      }
   }
   return nullptr;
}

[[nodiscard]] bool exports_api(const binding_plan& plan,
                               api_ref requested) noexcept {
   return plan.exports.empty() ||
          find_export(plan.exports, requested) != nullptr;
}

[[nodiscard]] bool method_hidden_by_export(const binding_plan& plan,
                                           const registry::snapshot& selected,
                                           api_ref requested,
                                           std::string_view method) noexcept {
   const auto* local_descriptor = selected.describe();
   if (local_descriptor != nullptr && !compatible(*local_descriptor, requested)) {
      local_descriptor = nullptr;
   }
   const auto* exported =
      plan.exports.empty()
         ? local_descriptor
         : find_export(plan.exports, requested);
   if (exported == nullptr) {
      return false;
   }
   if (const auto* exported_method = find_method(*exported, method)) {
      return exported_method->since_revision > requested.min_revision;
   }
   if (plan.exports.empty() || local_descriptor == nullptr) {
      return false;
   }
   return find_method(*local_descriptor, method) != nullptr;
}

void sort_interceptors(std::vector<interceptor_step>& interceptors) {
   std::sort(interceptors.begin(), interceptors.end(),
             [](const auto& left, const auto& right) {
                if (left.phase != right.phase) {
                   return static_cast<unsigned>(left.phase) <
                          static_cast<unsigned>(right.phase);
                }
                if (left.order != right.order) {
                   return left.order < right.order;
                }
                return left.id < right.id;
             });
}

void validate_interceptors(
   const std::vector<interceptor_step>& interceptors) {
   auto ids = std::set<std::string>{};
   for (const auto& step : interceptors) {
      if (step.id.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                               "API interceptor id must not be empty");
      }
      if (!step.handler) {
         FORGE_THROW_EXCEPTION(
            exceptions::protocol_error,
            "API interceptor handler must not be empty",
            forge::exceptions::ctx("interceptor", step.id));
      }
      if (!ids.insert(step.id).second) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                               "duplicate API interceptor id",
                               forge::exceptions::ctx("interceptor", step.id));
      }
   }
}

[[nodiscard]] bool has_before_interceptors(const binding_plan& plan) {
   return std::any_of(plan.interceptors.begin(), plan.interceptors.end(), [](const auto& step) {
      return step.handler && step.phase <= interceptor_phase::before_call;
   });
}

[[nodiscard]] contextual_dispatch_hook
make_before_interceptor_hook(const binding_plan& plan) {
   if (!has_before_interceptors(plan)) {
      return {};
   }
   auto before = std::vector<interceptor_step>{};
   for (const auto& step : plan.interceptors) {
      if (step.handler && step.phase <= interceptor_phase::before_call) {
         before.push_back(step);
      }
   }
   return [before = std::move(before)](frame& request, request_view typed_request,
                                       const canonical_request_encoder& encode) -> boost::asio::awaitable<void> {
      auto canonical_payload = encode();
      auto context = call_context{
         .id = request.id,
         .api = request.api,
         .method = request.method,
         .payload = std::move(canonical_payload),
         .codec = request.codec,
         .kind = request.kind,
         .request = typed_request,
      };
      context.meta = std::move(request.meta);
      try {
         for (const auto& step : before) {
            co_await step.handler(context);
            if (context.payload != encode()) {
               throw exceptions::protocol_error{"API interceptor must not mutate the canonical request payload"};
            }
         }
      } catch (...) {
         request.meta = std::move(context.meta);
         throw;
      }
      request.meta = std::move(context.meta);
   };
}

boost::asio::awaitable<void>
run_terminal_interceptors(const binding_plan& plan, frame& response) {
   const auto matched = std::any_of(plan.interceptors.begin(), plan.interceptors.end(),
                                    [&](const auto& step) {
                                       return step.handler &&
                                              (response.kind == frame_kind::error
                                                  ? step.phase == interceptor_phase::error
                                                  : step.phase == interceptor_phase::after_call);
                                    });
   if (!matched) {
      co_return;
   }
   auto context = make_context(response);
   for (const auto& step : plan.interceptors) {
      const auto matches =
         response.kind == frame_kind::error
            ? step.phase == interceptor_phase::error
            : step.phase == interceptor_phase::after_call;
      if (step.handler && matches) {
         co_await step.handler(context);
      }
   }
   response.meta = std::move(context.meta);
   response.payload = std::move(context.payload);
}

} // namespace

interceptor_builder& interceptor_builder::id(std::string value) {
   value_.id = std::move(value);
   return *this;
}

interceptor_builder&
interceptor_builder::phase(interceptor_phase value) noexcept {
   value_.phase = value;
   return *this;
}

interceptor_builder& interceptor_builder::order(int value) noexcept {
   value_.order = value;
   return *this;
}

interceptor_builder&
interceptor_builder::handler(interceptor_handler value) {
   value_.handler = std::move(value);
   return *this;
}

interceptor_step interceptor_builder::build() {
   return std::move(value_);
}

interceptor_builder interceptor() {
   return interceptor_builder{};
}

binding_plan binding_plan::pin(api_ref requested) const {
   if (local == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::incompatible_version,
                            "API binding plan has no local registry");
   }
   auto result = *this;
   if (!result.pinned_) {
      result.pinned_.emplace(local->pin(std::move(requested)));
   }
   return result;
}

const descriptor* binding_plan::describe(api_ref requested) const noexcept {
   if (pinned_) {
      const auto* selected = pinned_->describe();
      return selected != nullptr && compatible(*selected, requested) ? selected : nullptr;
   }
   return local == nullptr ? nullptr : local->describe(std::move(requested));
}

registry::snapshot binding_plan::selected_generation(api_ref requested) const {
   if (pinned_) {
      return *pinned_;
   }
   return local == nullptr ? registry::snapshot{} : local->pin(std::move(requested));
}

boost::asio::awaitable<frame> binding_plan::dispatch(frame request) const {
   if (local == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::incompatible_version,
                            "API binding plan has no local registry");
   }
   auto selected = selected_generation(request.api);
   if (!exports_api(*this, request.api) ||
       method_hidden_by_export(*this, selected, request.api, request.method)) {
      co_return make_api_not_exported_response(request);
   }

   auto before = make_before_interceptor_hook(*this);
   auto response = co_await selected.dispatch_contextual(
      std::move(request), trusted_invocation{}, std::move(before));
   co_await run_terminal_interceptors(*this, response);
   co_return response;
}

boost::asio::awaitable<frame>
binding_plan::dispatch_contextual(frame request, trusted_invocation trusted) const {
   if (local == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::incompatible_version,
                            "API binding plan has no local registry");
   }
   auto selected = selected_generation(request.api);
   if (!exports_api(*this, request.api) ||
       method_hidden_by_export(*this, selected, request.api, request.method)) {
      co_return make_api_not_exported_response(request);
   }

   auto before = make_before_interceptor_hook(*this);
   auto response = co_await selected.dispatch_contextual(
      std::move(request), std::move(trusted), std::move(before));
   co_await run_terminal_interceptors(*this, response);
   co_return response;
}

boost::asio::awaitable<frame>
binding_plan::dispatch_stream(
   frame request, std::shared_ptr<detail::stream_endpoint> input,
   std::shared_ptr<detail::stream_endpoint> output) const {
   if (local == nullptr) {
      fail_stream_endpoints(input, output);
      FORGE_THROW_EXCEPTION(exceptions::incompatible_version,
                            "API binding plan has no local registry");
   }
   auto selected = selected_generation(request.api);
   if (!exports_api(*this, request.api) ||
       method_hidden_by_export(*this, selected, request.api, request.method)) {
      fail_stream_endpoints(input, output);
      co_return make_api_not_exported_response(request);
   }

   try {
      auto before = make_before_interceptor_hook(*this);
      auto response = co_await selected.dispatch_stream_contextual(
         std::move(request), input, output, trusted_invocation{}, std::move(before));
      co_await run_terminal_interceptors(*this, response);
      if (response.kind == frame_kind::error) {
         fail_stream_endpoints(input, output);
      }
      co_return response;
   } catch (...) {
      fail_stream_endpoints(input, output);
      throw;
   }
}

boost::asio::awaitable<frame>
binding_plan::dispatch_stream_contextual(
   frame request, std::shared_ptr<detail::stream_endpoint> input,
   std::shared_ptr<detail::stream_endpoint> output,
   trusted_invocation trusted) const {
   if (local == nullptr) {
      fail_stream_endpoints(input, output);
      FORGE_THROW_EXCEPTION(exceptions::incompatible_version,
                            "API binding plan has no local registry");
   }
   auto selected = selected_generation(request.api);
   if (!exports_api(*this, request.api) ||
       method_hidden_by_export(*this, selected, request.api, request.method)) {
      fail_stream_endpoints(input, output);
      co_return make_api_not_exported_response(request);
   }

   try {
      auto before = make_before_interceptor_hook(*this);
      auto response = co_await selected.dispatch_stream_contextual(
         std::move(request), input, output, std::move(trusted), std::move(before));
      co_await run_terminal_interceptors(*this, response);
      if (response.kind == frame_kind::error) {
         fail_stream_endpoints(input, output);
      }
      co_return response;
   } catch (...) {
      fail_stream_endpoints(input, output);
      throw;
   }
}

binding_builder& binding_builder::serve(const registry& apis) {
   plan_.local = &apis;
   return *this;
}

binding_builder& binding_builder::serve(const view& apis) {
   plan_.local = &apis.registry_ref();
   return *this;
}

binding_builder&
binding_builder::interceptor(interceptor_step step) {
   plan_.interceptors.push_back(std::move(step));
   return *this;
}

binding_plan binding_builder::build() {
   sort_interceptors(plan_.interceptors);
   validate_interceptors(plan_.interceptors);
   return std::move(plan_);
}

binding_builder binding() {
   return {};
}

} // namespace forge::api::core
