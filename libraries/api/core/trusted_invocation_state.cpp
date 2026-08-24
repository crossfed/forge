module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>

module forge.api.core.trusted_invocation:trusted_invocation_state;

namespace forge::api::core::detail {

void trusted_invocation_state::insert(std::type_index type, std::shared_ptr<const void> value) {
   if (!values_.emplace(type, std::move(value)).second) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "duplicate trusted invocation value");
   }
}

bool trusted_invocation_state::empty() const noexcept {
   return values_.empty();
}

const void* trusted_invocation_state::find_exact(std::type_index type) const noexcept {
   const auto iterator = values_.find(type);
   return iterator == values_.end() ? nullptr : iterator->second.get();
}

} // namespace forge::api::core::detail
