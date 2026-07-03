#include <concepts>
#include <type_traits>
#include <utility>

import forge.objectdb.store;
import forge.objectdb.driver;
import forge.objectdb.session;
import forge.objectdb.rocksdb;

int main() {
   static_assert(std::derived_from<forge::objectdb::rocksdb::driver, forge::objectdb::driver>);
   static_assert(std::derived_from<forge::objectdb::rocksdb::session, forge::objectdb::session>);
   static_assert(std::same_as<decltype(std::declval<forge::objectdb::rocksdb::driver&>().flush()), void>);
   static_assert(!std::same_as<decltype(std::declval<forge::objectdb::rocksdb::driver&>().async_flush(true)), void>);
   auto cfg = forge::objectdb::rocksdb::config{};
   cfg.path = "unused";
   cfg.family = "objectdb";
   return cfg.family == "objectdb" ? 0 : 1;
}
