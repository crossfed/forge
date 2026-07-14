#pragma once

namespace forge::plugins::db::store {

struct opened_store {
   std::shared_ptr<forge::db::core::driver> driver;
   std::shared_ptr<forge::db::object::store> objects;
   std::shared_ptr<forge::db::blob::store> blobs;
   std::shared_ptr<forge::db::revision::store> revisions;
};

} // namespace forge::plugins::db::store
