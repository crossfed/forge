#pragma once

namespace forge::plugins::db::store {

struct managed_store {
   std::string name;
   std::string driver_name;
   std::string path;
   store_options options;
   std::shared_ptr<forge::db::core::driver> driver;
   std::shared_ptr<forge::db::object::store> objects;
   std::shared_ptr<forge::db::blob::store> blobs;
   bool opened = false;
   bool started = false;
};

} // namespace forge::plugins::db::store
