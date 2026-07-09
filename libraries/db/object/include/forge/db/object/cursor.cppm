module;

#include <forge/exceptions/macros.hpp>

export module forge.db.object.cursor;

import forge.db.core.record;
import forge.db.object.exceptions;

export namespace forge::db::object {

inline void validate_page_request(const forge::db::core::page_request& request) {
   if (request.limit == 0 || request.limit > forge::db::core::max_page_limit) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_cursor, "invalid db object page limit");
   }
}

} // namespace forge::db::object
