#pragma once

#include <memory>

namespace forge::blobdb {

struct store::impl {
   impl(std::shared_ptr<forge::db::driver> driver_value, store::config config_value);

   std::shared_ptr<forge::db::driver> driver;
   store::config config;
};

} // namespace forge::blobdb
