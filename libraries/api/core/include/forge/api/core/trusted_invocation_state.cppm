module;

#include <memory>
#include <typeindex>
#include <unordered_map>

export module forge.api.core.trusted_invocation:trusted_invocation_state;

namespace forge::api::core::detail {

struct trusted_invocation_state final {
   std::unordered_map<std::type_index, std::shared_ptr<const void>> values_;
};

} // namespace forge::api::core::detail
