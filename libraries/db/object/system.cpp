module;

#include <memory>
#include <cstddef>
#include <cstdint>
#include <vector>

module forge.db.object.system;

import forge.db.object.object;

#include "details/record_key.hxx"

namespace forge::db::object::system {

std::shared_ptr<forge::db::core::driver> access::driver(const store& value) {
   return value.driver();
}

forge::db::core::family access::family(const store& value) {
   return value.family();
}

forge::db::core::record_key access::record_key(forge::ids::object_id id) {
   return detail::record_key::object(id);
}

} // namespace forge::db::object::system
