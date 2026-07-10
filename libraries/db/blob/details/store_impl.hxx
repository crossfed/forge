#pragma once

#include <memory>

namespace forge::db::blob {

struct store::impl {
   impl(std::shared_ptr<forge::db::core::driver> driver_value, store::config config_value);

   std::shared_ptr<forge::db::core::driver> driver;
   store::config config;
};

} // namespace forge::db::blob
