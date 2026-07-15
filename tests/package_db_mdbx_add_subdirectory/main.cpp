#include <concepts>

import forge.db.core.driver;
import forge.db.mdbx.driver;

int main() {
   static_assert(std::derived_from<
                 forge::db::mdbx::driver,
                 forge::db::core::driver>);
   return 0;
}
