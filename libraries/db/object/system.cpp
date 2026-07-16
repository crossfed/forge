module;

#include <memory>
#include <cstddef>
#include <cstdint>
#include <string>
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

bool access::joined(const store& value,
                    const forge::db::core::transaction& active) {
   const auto object_family = value.family();
   auto participant = std::string{"forge.db.object:"};
   participant.append(std::to_string(object_family.name.size()));
   participant.push_back(':');
   participant.append(object_family.name);
   return active.has_participant(participant);
}

forge::db::core::record_key access::record_key(forge::db::ids::object_id id) {
   return detail::record_key::object(id);
}

} // namespace forge::db::object::system
