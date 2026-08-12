module;

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>

module forge.app.service_registry;

namespace forge::app {

struct service_registry::impl {
   mutable std::shared_mutex mutex;
   std::unordered_map<std::type_index, std::shared_ptr<void>> entries;
   bool publication_closed = false;
};

service_registry::service_registry() : impl_{std::make_unique<impl>()} {}

service_registry::~service_registry() = default;

service_view service_registry::view() const noexcept {
   return service_view{*this};
}

void service_registry::publish_erased(std::type_index type, std::shared_ptr<void> service) {
   auto lock = std::unique_lock{impl_->mutex};
   if (impl_->publication_closed) {
      throw exceptions::service_publication_closed{"application service publication is closed"};
   }
   if (!service) {
      throw exceptions::invalid_service{"cannot publish a null application service"};
   }
   if (impl_->entries.contains(type)) {
      throw exceptions::service_already_published{"application service type is already published"};
   }
   impl_->entries.emplace(type, std::move(service));
}

std::shared_ptr<void> service_registry::try_get_erased(std::type_index type) const {
   auto lock = std::shared_lock{impl_->mutex};
   const auto iterator = impl_->entries.find(type);
   if (iterator == impl_->entries.end()) {
      return {};
   }
   return iterator->second;
}

std::shared_ptr<void> service_registry::get_erased(std::type_index type) const {
   auto result = try_get_erased(type);
   if (!result) {
      throw exceptions::service_missing{"required application service is not available"};
   }
   return result;
}

void service_registry::close() {
   auto lock = std::unique_lock{impl_->mutex};
   impl_->publication_closed = true;
}

void service_registry::clear() {
   auto lock = std::unique_lock{impl_->mutex};
   impl_->publication_closed = true;
   impl_->entries.clear();
}

} // namespace forge::app
