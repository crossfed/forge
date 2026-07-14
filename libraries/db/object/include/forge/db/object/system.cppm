module;

#include <memory>
#include <typeindex>

export module forge.db.object.system;

import forge.ids.object_id;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.object.index;
import forge.db.object.store;

export namespace forge::db::object::system {

class access final {
 public:
   [[nodiscard]] static std::shared_ptr<forge::db::core::driver> driver(const store& value);
   [[nodiscard]] static forge::db::core::family family(const store& value);
   [[nodiscard]] static forge::db::core::record_key record_key(forge::ids::object_id id);

   template <system_object_model Object>
   static void register_object(store& value) {
      value.template register_system_object<Object>();
   }
};

} // namespace forge::db::object::system
