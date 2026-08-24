#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>

namespace forge::api::core {

struct trusted_invocation::impl final {
   std::unordered_map<std::type_index, std::shared_ptr<const void>> values;
};

} // namespace forge::api::core
