module;

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

export module forge.api.core.registry;

export import forge.api.core.descriptor;
export import forge.api.core.error_projection;
export import forge.api.core.handle;
export import forge.api.core.connection;
export import forge.api.core.trusted_invocation;

export namespace forge::api::core {

class registry : public service_mount {
 public:
   class snapshot;

   registry();
   ~registry();

   registry(const registry&) = delete;
   registry& operator=(const registry&) = delete;

   template <typename Interface> void install(descriptor descriptor, std::shared_ptr<Interface> implementation) {
      static_assert(std::is_class_v<Interface>, "API interface must be a class type");
      static_assert(std::has_virtual_destructor_v<Interface>, "API interface must have a virtual destructor");
      static_assert(local_interface<Interface>, "Interface must opt in to forge::api::core::surface::local");
      if (!implementation) {
         throw exceptions::protocol_error{"cannot install null API implementation"};
      }
      descriptor.interface_type = typeid(Interface);
      descriptor.supported_surfaces = Interface::api_surface;
      register_api(std::move(descriptor), std::move(implementation), typeid(Interface));
   }

   template <typename Interface> void install(std::shared_ptr<Interface> implementation) {
      install<Interface>(Interface::describe(), std::move(implementation));
   }

   template <typename Interface> [[nodiscard]] handle<Interface> try_get(api_ref requested) const;

   template <typename Interface> [[nodiscard]] handle<Interface> get(api_ref requested) const;

   [[nodiscard]] snapshot pin(api_ref requested) const;
   [[nodiscard]] const descriptor* describe(api_ref requested) const;
   boost::asio::awaitable<frame> dispatch(frame request) const;
   boost::asio::awaitable<frame> dispatch_contextual(frame request, trusted_invocation trusted) const;
   boost::asio::awaitable<frame>
   dispatch_contextual(frame request, trusted_invocation trusted, contextual_dispatch_hook before) const;
   boost::asio::awaitable<frame>
   dispatch_stream(frame request, std::shared_ptr<detail::stream_endpoint> input,
                   std::shared_ptr<detail::stream_endpoint> output) const;
   boost::asio::awaitable<frame>
   dispatch_stream_contextual(frame request, std::shared_ptr<detail::stream_endpoint> input,
                              std::shared_ptr<detail::stream_endpoint> output,
                              trusted_invocation trusted) const;
   boost::asio::awaitable<frame>
   dispatch_stream_contextual(frame request, std::shared_ptr<detail::stream_endpoint> input,
                              std::shared_ptr<detail::stream_endpoint> output,
                              trusted_invocation trusted, contextual_dispatch_hook before) const;
   [[nodiscard]] std::size_t size() const;
   void clear();

 private:
   struct entry {
      descriptor descriptor;
      std::shared_ptr<void> implementation;
      std::type_index interface_type = typeid(void);
   };

   static std::string key_for(std::string_view id, std::uint16_t major);
   [[nodiscard]] std::shared_ptr<const entry> find(api_ref requested) const;

   void register_api(descriptor value, std::shared_ptr<void> implementation, std::type_index type) override;

   mutable std::mutex entries_mutex_;
   std::unordered_map<std::string, std::shared_ptr<const entry>> entries_;
};

class registry::snapshot {
 public:
   snapshot() = default;

   [[nodiscard]] explicit operator bool() const noexcept {
      return static_cast<bool>(entry_);
   }

   [[nodiscard]] const descriptor* describe() const noexcept {
      return entry_ == nullptr ? nullptr : &entry_->descriptor;
   }

   template <typename Interface> [[nodiscard]] handle<Interface> try_get() const {
      static_assert(local_interface<Interface>, "Interface must opt in to forge::api::core::surface::local");
      if (entry_ == nullptr || entry_->interface_type != typeid(Interface)) {
         return {};
      }
      return handle<Interface>{std::static_pointer_cast<Interface>(entry_->implementation)};
   }

   template <typename Interface> [[nodiscard]] handle<Interface> get() const {
      static_assert(local_interface<Interface>, "Interface must opt in to forge::api::core::surface::local");
      auto result = try_get<Interface>();
      if (!result) {
         throw exceptions::protocol_error{"required API is not available"};
      }
      return result;
   }

   boost::asio::awaitable<frame> dispatch_contextual(frame request, trusted_invocation trusted) const;
   boost::asio::awaitable<frame>
   dispatch_contextual(frame request, trusted_invocation trusted, contextual_dispatch_hook before) const;
   boost::asio::awaitable<frame>
   dispatch_stream_contextual(frame request, std::shared_ptr<detail::stream_endpoint> input,
                              std::shared_ptr<detail::stream_endpoint> output,
                              trusted_invocation trusted) const;
   boost::asio::awaitable<frame>
   dispatch_stream_contextual(frame request, std::shared_ptr<detail::stream_endpoint> input,
                              std::shared_ptr<detail::stream_endpoint> output,
                              trusted_invocation trusted, contextual_dispatch_hook before) const;

 private:
   friend class registry;

   explicit snapshot(std::shared_ptr<const registry::entry> entry) : entry_(std::move(entry)) {}

   static boost::asio::awaitable<frame>
   dispatch_contextual_entry(std::shared_ptr<const registry::entry> entry, frame request,
                             trusted_invocation trusted, contextual_dispatch_hook before);
   static boost::asio::awaitable<frame>
   dispatch_stream_contextual_entry(std::shared_ptr<const registry::entry> entry, frame request,
                                    std::shared_ptr<detail::stream_endpoint> input,
                                    std::shared_ptr<detail::stream_endpoint> output,
                                    trusted_invocation trusted, contextual_dispatch_hook before);

   std::shared_ptr<const registry::entry> entry_;
};

template <typename Interface> handle<Interface> registry::try_get(api_ref requested) const {
   static_assert(local_interface<Interface>, "Interface must opt in to forge::api::core::surface::local");
   return pin(std::move(requested)).try_get<Interface>();
}

template <typename Interface> handle<Interface> registry::get(api_ref requested) const {
   static_assert(local_interface<Interface>, "Interface must opt in to forge::api::core::surface::local");
   return pin(std::move(requested)).get<Interface>();
}

class installer {
 public:
   explicit installer(registry& apis) : apis_(&apis) {}

   template <typename Interface> void install(descriptor descriptor, std::shared_ptr<Interface> implementation) {
      static_assert(local_interface<Interface>, "Interface must opt in to forge::api::core::surface::local");
      apis_->install<Interface>(std::move(descriptor), std::move(implementation));
   }

   template <typename Interface> void install(std::shared_ptr<Interface> implementation) {
      apis_->install<Interface>(std::move(implementation));
   }

 private:
   registry* apis_ = nullptr;
};

using provider = installer;

class view {
 public:
   explicit view(const registry& apis) : apis_(&apis) {}

   template <typename Interface> [[nodiscard]] handle<Interface> try_get(api_ref requested) const {
      static_assert(local_interface<Interface>, "Interface must opt in to forge::api::core::surface::local");
      return apis_->try_get<Interface>(std::move(requested));
   }

   template <typename Interface> [[nodiscard]] handle<Interface> get(api_ref requested) const {
      static_assert(local_interface<Interface>, "Interface must opt in to forge::api::core::surface::local");
      return apis_->get<Interface>(std::move(requested));
   }

   [[nodiscard]] const registry& registry_ref() const noexcept {
      return *apis_;
   }

 private:
   const registry* apis_ = nullptr;
};

} // namespace forge::api
