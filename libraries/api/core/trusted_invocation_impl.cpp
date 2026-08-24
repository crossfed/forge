module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <typeindex>
#include <utility>

module forge.api.core.trusted_invocation;

#include "details/trusted_invocation_impl.hxx"

namespace forge::api::core {

trusted_invocation::trusted_invocation(std::shared_ptr<const impl> value) noexcept
    : impl_{std::move(value)} {}

trusted_invocation::~trusted_invocation() = default;

bool trusted_invocation::empty() const noexcept {
   return !impl_ || impl_->values.empty();
}

const void* trusted_invocation::find_exact(std::type_index type) const noexcept {
   if (!impl_) {
      return nullptr;
   }
   const auto iterator = impl_->values.find(type);
   return iterator == impl_->values.end() ? nullptr : iterator->second.get();
}

trusted_invocation_builder::trusted_invocation_builder()
    : impl_{std::make_unique<trusted_invocation::impl>()} {}

trusted_invocation_builder::~trusted_invocation_builder() = default;
trusted_invocation_builder::trusted_invocation_builder(trusted_invocation_builder&&) noexcept = default;
trusted_invocation_builder&
trusted_invocation_builder::operator=(trusted_invocation_builder&&) noexcept = default;

void trusted_invocation_builder::insert(std::type_index type, std::shared_ptr<const void> value) {
   if (!impl_) {
      impl_ = std::make_unique<trusted_invocation::impl>();
   }
   if (!impl_->values.emplace(type, std::move(value)).second) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "duplicate trusted invocation value");
   }
}

trusted_invocation trusted_invocation_builder::build() && {
   if (!impl_) {
      return {};
   }
   auto value = std::shared_ptr<const trusted_invocation::impl>{std::move(impl_)};
   return trusted_invocation{std::move(value)};
}

} // namespace forge::api::core
