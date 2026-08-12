module;

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

export module forge.app.connect_context;

import forge.api.core.registry;
import forge.app.service_registry;

export namespace forge::app {

class connect_context {
 public:
   connect_context(forge::api::core::view apis, service_registry& services) noexcept;

   template <typename Interface>
   [[nodiscard]] forge::api::core::handle<Interface>
   api(forge::api::core::api_ref requested = Interface::ref()) const {
      static_assert(forge::api::core::local_interface<Interface>,
                    "Interface must opt in to forge::api::core::surface::local");
      return apis_.get<Interface>(std::move(requested));
   }

   template <typename Service> void publish(std::shared_ptr<Service> service) {
      static_assert(std::is_object_v<Service>, "service type must be an object type");
      static_assert(std::same_as<Service, std::remove_cv_t<Service>>,
                    "service publication requires an unqualified type");
      services_->publish<Service>(std::move(service));
   }

   template <typename Service> [[nodiscard]] std::shared_ptr<Service> service() const {
      return services_->view().get<Service>();
   }

   template <typename Service> [[nodiscard]] std::shared_ptr<Service> try_service() const {
      return services_->view().try_get<Service>();
   }

 private:
   forge::api::core::view apis_;
   service_registry* services_ = nullptr;
};

} // namespace forge::app
