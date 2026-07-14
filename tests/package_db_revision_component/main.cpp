#include <concepts>
#include <cstdint>

import forge.db.core.driver;
import forge.db.object.index;
import forge.db.object.system;
import forge.db.revision.store;
import forge.db.revision.transaction;
import forge.db.revision.types;

int main() {
   static_assert(std::same_as<forge::db::revision::revision_id_t, std::uint64_t>);
   static_assert(forge::db::object::system_object_model<forge::db::revision::state_index>);
   static_assert(forge::db::object::system_object_model<forge::db::revision::entry_index>);
   static_assert(forge::db::object::system_object_model<forge::db::revision::delta_index>);

   const auto options = forge::db::revision::prune_options{
      .max_revisions = 10,
      .max_deltas = 100,
   };
   return options.max_revisions == 10 && forge::db::revision::state_id.instance == 0 ? 0 : 1;
}
