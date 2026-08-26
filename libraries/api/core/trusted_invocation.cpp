module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>

module forge.api.core.trusted_invocation;

import :trusted_invocation_state;

namespace forge::api::core {

trusted_invocation::trusted_invocation(std::shared_ptr<const detail::trusted_invocation_state> value) noexcept
    : state_{std::move(value)} {}

trusted_invocation::~trusted_invocation() = default;

bool trusted_invocation::empty() const noexcept {
   return !state_ || state_->values_.empty();
}

const void* trusted_invocation::find_exact(std::type_index type) const noexcept {
   if (!state_) {
      return nullptr;
   }
   const auto iterator = state_->values_.find(type);
   return iterator == state_->values_.end() ? nullptr : iterator->second.get();
}

trusted_invocation_builder::trusted_invocation_builder()
    : state_{std::make_unique<detail::trusted_invocation_state>()} {}

trusted_invocation_builder::~trusted_invocation_builder() = default;
trusted_invocation_builder::trusted_invocation_builder(trusted_invocation_builder&&) noexcept = default;
trusted_invocation_builder&
trusted_invocation_builder::operator=(trusted_invocation_builder&&) noexcept = default;

void trusted_invocation_builder::insert(std::type_index type, std::shared_ptr<const void> value) {
   if (!state_) {
      state_ = std::make_unique<detail::trusted_invocation_state>();
   }
   if (!state_->values_.emplace(type, std::move(value)).second) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "duplicate trusted invocation value");
   }
}

trusted_invocation trusted_invocation_builder::build() && {
   if (!state_) {
      return {};
   }
   auto value = std::shared_ptr<const detail::trusted_invocation_state>{std::move(state_)};
   return trusted_invocation{std::move(value)};
}

} // namespace forge::api::core
