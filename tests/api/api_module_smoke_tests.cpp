#include <boost/test/unit_test.hpp>

#include <memory>
#include <utility>

import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.registry;
import forge.api.core.binding;

BOOST_AUTO_TEST_SUITE(api_module_smoke_suite)

BOOST_AUTO_TEST_CASE(leaf_modules_can_be_imported_without_aggregate) {
   const auto available = forge::api::core::descriptor{
       .id = {.value = "module.smoke"},
       .version = {.major = 1, .revision = 4},
   };
   const auto requested = forge::api::core::api_ref{
       .id = {.value = "module.smoke"},
       .major = 1,
       .min_revision = 3,
   };

   auto registry = forge::api::core::registry{};
   auto installer = forge::api::core::installer{registry};
   auto view = forge::api::core::view{registry};
   auto plan = std::move(forge::api::core::binding().serve(registry)).build();
   const auto frame = forge::api::core::frame{
       .kind = forge::api::core::frame_kind::cancel,
       .api = requested,
       .codec = {.value = "forge.raw"},
   };

   static_cast<void>(installer);
   BOOST_TEST(forge::api::core::compatible(available, requested));
   BOOST_TEST(plan.local == &registry);
   BOOST_TEST(&view.registry_ref() == &registry);
   BOOST_CHECK(frame.kind == forge::api::core::frame_kind::cancel);
}

BOOST_AUTO_TEST_SUITE_END()
