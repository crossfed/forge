module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <typeindex>
#include <utility>

module forge.api.core.trusted_invocation;

#include "details/trusted_invocation_state.hxx"

namespace forge::api::core {

void trusted_invocation::state::insert(std::type_index type, std::shared_ptr<const void> value) {
   if (!values_.emplace(type, std::move(value)).second) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "duplicate trusted invocation value");
   }
}

bool trusted_invocation::state::empty() const noexcept {
   return values_.empty();
}

const void* trusted_invocation::state::find_exact(std::type_index type) const noexcept {
   const auto iterator = values_.find(type);
   return iterator == values_.end() ? nullptr : iterator->second.get();
}

} // namespace forge::api::core
