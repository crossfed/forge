module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

export module forge.api.core.binding;

export import forge.api.core.context;
export import forge.api.core.registry;
export import forge.api.core.trusted_invocation;

export namespace forge::api::core {

enum class interceptor_phase : std::uint8_t {
   observe = 1,
   authorize = 2,
   limits = 3,
   before_call = 4,
   after_call = 5,
   error = 6,
};

using interceptor_handler = std::function<boost::asio::awaitable<void>(call_context&)>;

struct interceptor_step {
   std::string id;
   interceptor_phase phase = interceptor_phase::before_call;
   int order = 0;
   interceptor_handler handler;
};

class interceptor_builder {
 public:
   interceptor_builder& id(std::string value);
   interceptor_builder& phase(interceptor_phase value) noexcept;
   interceptor_builder& order(int value) noexcept;
   interceptor_builder& handler(interceptor_handler value);
   [[nodiscard]] interceptor_step build();

 private:
   interceptor_step value_;
};

[[nodiscard]] interceptor_builder interceptor();

class pinned_binding_plan;

struct binding_plan {
   const registry* local = nullptr;
   std::vector<descriptor> exports;
   std::vector<api_ref> peer_requirements;
   std::vector<interceptor_step> interceptors;

   [[nodiscard]] pinned_binding_plan pin(api_ref requested) const;
   [[nodiscard]] const descriptor* describe(api_ref requested) const;

   template <typename Interface> [[nodiscard]] handle<Interface> get(api_ref requested) const;

   boost::asio::awaitable<frame> dispatch(frame request) const;
   boost::asio::awaitable<frame> dispatch_contextual(frame request, trusted_invocation trusted) const;
   boost::asio::awaitable<frame>
   dispatch_stream(frame request,
                   std::shared_ptr<detail::stream_endpoint> input,
                   std::shared_ptr<detail::stream_endpoint> output) const;
   boost::asio::awaitable<frame>
   dispatch_stream_contextual(frame request,
                              std::shared_ptr<detail::stream_endpoint> input,
                              std::shared_ptr<detail::stream_endpoint> output,
                              trusted_invocation trusted) const;
};

class pinned_binding_plan {
 public:
   [[nodiscard]] const descriptor* describe(api_ref requested) const noexcept;

   template <typename Interface> [[nodiscard]] handle<Interface> get(api_ref requested) const {
      static_assert(local_interface<Interface>, "Interface must opt in to forge::api::core::surface::local");
      const auto* selected_descriptor = describe(requested);
      if (selected_descriptor == nullptr) {
         throw exceptions::protocol_error{"required API is not available"};
      }
      return selected_.get<Interface>();
   }

   boost::asio::awaitable<frame> dispatch(frame request) const;
   boost::asio::awaitable<frame> dispatch_contextual(frame request, trusted_invocation trusted) const;
   boost::asio::awaitable<frame>
   dispatch_stream(frame request,
                   std::shared_ptr<detail::stream_endpoint> input,
                   std::shared_ptr<detail::stream_endpoint> output) const;
   boost::asio::awaitable<frame>
   dispatch_stream_contextual(frame request,
                              std::shared_ptr<detail::stream_endpoint> input,
                              std::shared_ptr<detail::stream_endpoint> output,
                              trusted_invocation trusted) const;

 private:
   friend struct binding_plan;

   pinned_binding_plan(binding_plan plan, registry::snapshot selected);

   binding_plan plan_;
   registry::snapshot selected_;
};

template <typename Interface> handle<Interface> binding_plan::get(api_ref requested) const {
   auto selected = pin(requested);
   return selected.get<Interface>(std::move(requested));
}

class binding_builder {
 public:
   binding_builder& serve(const registry& apis);
   binding_builder& serve(const view& apis);

   template <typename Interface> binding_builder& export_api(api_ref api) {
      static_assert(remote_interface<Interface>, "Interface must opt in to forge::api::core::surface::remote");
      auto descriptor = Interface::describe();
      if (api.min_revision > descriptor.version.revision) {
         FORGE_THROW_EXCEPTION(exceptions::incompatible_version, "API export revision exceeds implementation revision",
                               forge::exceptions::ctx("api", api.id.value),
                               forge::exceptions::ctx("requested_revision", api.min_revision),
                               forge::exceptions::ctx("implementation_revision", descriptor.version.revision));
      }
      descriptor.id = std::move(api.id);
      descriptor.version.major = api.major;
      descriptor.version.revision = api.min_revision;
      descriptor.methods.erase(
          std::remove_if(descriptor.methods.begin(), descriptor.methods.end(),
                         [&](const auto& method) { return method.since_revision > api.min_revision; }),
          descriptor.methods.end());
      plan_.exports.push_back(std::move(descriptor));
      return *this;
   }

   template <typename Interface> binding_builder& require_peer_api(api_ref api) {
      static_assert(remote_interface<Interface>, "Interface must opt in to forge::api::core::surface::remote");
      plan_.peer_requirements.push_back(std::move(api));
      return *this;
   }

   binding_builder& interceptor(interceptor_step step);

   [[nodiscard]] binding_plan build();

 private:
   binding_plan plan_;
};

[[nodiscard]] binding_builder binding();

} // namespace forge::api::core
