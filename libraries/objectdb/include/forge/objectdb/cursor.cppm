module;

#include <forge/exceptions/macros.hpp>

export module forge.objectdb.cursor;

import forge.db.record;
import forge.objectdb.exceptions;

export namespace forge::objectdb {

using forge::db::cursor;
using forge::db::default_page_limit;
using forge::db::max_page_limit;
using forge::db::page_request;

inline void validate_page_request(const page_request& request) {
   if (request.limit == 0 || request.limit > max_page_limit) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_cursor, "invalid objectdb page limit");
   }
}

} // namespace forge::objectdb
