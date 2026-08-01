#include <concepts>
#include <cstdint>

import forge.db.authenticated.proof;
import forge.db.authenticated.store;
import forge.db.authenticated.transaction;
import forge.db.authenticated.types;

int main() {
   static_assert(std::same_as<forge::db::authenticated::version_id_t, std::uint64_t>);

   const auto options = forge::db::authenticated::prune_options{
       .max_versions = 32,
       .max_garbage_records = 1'024,
   };
   const auto request = forge::db::authenticated::range_request{
       .limit = 128,
       .reverse = true,
   };
   return options.max_versions == 32 && request.limit == 128 && request.reverse ? 0 : 1;
}
