module;

#include <concepts>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <utility>

export module forge.app.service_registry;

import forge.app.exceptions;

export namespace forge::app {

class service_registry;

class service_view {
 public:
   service_view() = default;

   template <typename Service> [[nodiscard]] std::shared_ptr<Service> get() const;

 private:
   friend class service_registry;

   explicit service_view(const service_registry& registry) noexcept : registry_{&registry} {}

   const service_registry* registry_ = nullptr;
};

class service_registry {
 public:
   service_registry();
   ~service_registry();

   service_registry(const service_registry&) = delete;
   service_registry& operator=(const service_registry&) = delete;
   service_registry(service_registry&&) = delete;
   service_registry& operator=(service_registry&&) = delete;

   template <typename Service> void publish(std::shared_ptr<Service> service) {
      static_assert(std::is_object_v<Service>, "service type must be an object type");
      static_assert(std::same_as<Service, std::remove_cv_t<Service>>,
                    "service publication requires an unqualified type");
      publish_erased(typeid(Service), std::move(service));
   }

   [[nodiscard]] service_view view() const noexcept;
   void close() noexcept;
   void clear() noexcept;

 private:
   friend class service_view;

   void publish_erased(std::type_index type, std::shared_ptr<void> service);
   [[nodiscard]] std::shared_ptr<void> get_erased(std::type_index type) const;

   struct impl;
   std::unique_ptr<impl> impl_;
};

template <typename Service> std::shared_ptr<Service> service_view::get() const {
   static_assert(std::is_object_v<Service>, "service type must be an object type");
   static_assert(std::same_as<Service, std::remove_cv_t<Service>>, "service lookup requires an unqualified type");
   if (registry_ == nullptr) {
      throw exceptions::service_missing{"required application service is not available"};
   }
   return std::static_pointer_cast<Service>(registry_->get_erased(typeid(Service)));
}

} // namespace forge::app
