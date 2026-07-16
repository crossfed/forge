module;

#include <span>

module forge.db.blob.types;

import forge.ids.object_id;
import forge.raw.raw;

namespace forge::db::blob {

owner_ref::owner_ref(forge::ids::object_id value) {
   const auto packed = forge::raw::pack(value);
   const auto encoded = std::as_bytes(std::span{packed});
   bytes.assign(encoded.begin(), encoded.end());
}

} // namespace forge::db::blob
