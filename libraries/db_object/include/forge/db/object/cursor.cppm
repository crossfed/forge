module;

#include <forge/exceptions/macros.hpp>

export module forge.db.object.cursor;

import forge.db.record;
import forge.db.object.exceptions;

export namespace forge::db::object {

using forge::db::cursor;
using forge::db::default_page_limit;
using forge::db::max_page_limit;
using forge::db::page_request;

inline void validate_page_request(const page_request& request) {
   if (request.limit == 0 || request.limit > max_page_limit) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_cursor, "invalid db object page limit");
   }
}

} // namespace forge::db::object
