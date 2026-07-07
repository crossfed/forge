#include <concepts>
#include <type_traits>
#include <utility>

import forge.db.driver;
import forge.db.rocksdb;

int main() {
   static_assert(std::derived_from<forge::db::rocksdb::driver, forge::db::driver>);
   static_assert(std::same_as<decltype(std::declval<forge::db::rocksdb::driver&>().flush()), void>);
   static_assert(!std::same_as<decltype(std::declval<forge::db::rocksdb::driver&>().async_flush(true)), void>);
   auto cfg = forge::db::rocksdb::config{};
   cfg.path = "unused";
   cfg.families.emplace_back("objectdb");
   return cfg.families.back().name == "objectdb" ? 0 : 1;
}
