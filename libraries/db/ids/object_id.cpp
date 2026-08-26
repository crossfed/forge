module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>
#include <limits>
#include <string>

module forge.db.ids.object_id;

import forge.exceptions;
import forge.variant.value;

namespace forge::db::ids {

void to_variant(const object_id& value, forge::variant& out) {
   out = forge::mutable_variant_object{}("space", static_cast<std::uint64_t>(value.space))(
      "type",
      static_cast<std::uint64_t>(value.type))("instance", value.instance);
}

void from_variant(const forge::variant& input, object_id& out) {
   const auto& object = input.get_object();
   auto decoded_space = std::uint64_t{};
   auto decoded_type = std::uint64_t{};
   forge::from_variant(object["space"], decoded_space);
   forge::from_variant(object["type"], decoded_type);
   forge::from_variant(object["instance"], out.instance);
   if (decoded_space > std::numeric_limits<std::uint8_t>::max()) {
      FORGE_THROW("object_id space exceeds uint8 range", forge::exceptions::ctx("space", decoded_space));
   }
   if (decoded_type > std::numeric_limits<std::uint16_t>::max()) {
      FORGE_THROW("object_id type exceeds uint16 range", forge::exceptions::ctx("type", decoded_type));
   }
   out.space = static_cast<std::uint8_t>(decoded_space);
   out.type = static_cast<std::uint16_t>(decoded_type);
}

std::string to_string(object_id value) {
   return std::to_string(static_cast<std::uint64_t>(value.space)) + "/"
        + std::to_string(static_cast<std::uint64_t>(value.type)) + "/" + std::to_string(value.instance);
}

} // namespace forge::db::ids
