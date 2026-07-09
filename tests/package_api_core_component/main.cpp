#include <utility>

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.registry;
import forge.api.core.binding;

int main() {
   auto registry = forge::api::core::registry{};
   const auto plan = std::move(forge::api::core::binding().serve(registry)).build();
   const auto available = forge::api::core::descriptor{
       .id = {.value = "package.smoke"},
       .version = {.major = 1, .revision = 2},
   };
   const auto requested = forge::api::core::api_ref{
       .id = {.value = "package.smoke"},
       .major = 1,
       .min_revision = 2,
   };
   return forge::api::core::compatible(available, requested) && plan.local == &registry ? 0 : 1;
}
