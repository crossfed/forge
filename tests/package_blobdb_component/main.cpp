#include <cstddef>
#include <vector>

import forge.blobdb.store;
import forge.blobdb.types;
import forge.db.record;

int main() {
   auto cfg = forge::blobdb::store::config{};
   cfg.data_family = forge::db::family{"blobdb.data"};
   cfg.refs_family = forge::db::family{"blobdb.refs"};
   auto id = forge::blobdb::digest{std::vector<std::byte>{std::byte{1}}};
   return id.empty() ? 1 : 0;
}
