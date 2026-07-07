module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.plugins.db.object.exceptions;

import forge.exceptions;

export namespace forge::plugins::db::object {

class exceptions {
 public:
   enum class code : std::uint16_t {
      invalid_config = 1,
      duplicate_store = 2,
      unknown_store = 3,
      stopped = 4,
      startup_failed = 5,
      invalid_argument = 6,
      internal_error = 255,
   };

   using invalid_config = forge::exceptions::coded_exception<code, code::invalid_config>;
   using duplicate_store = forge::exceptions::coded_exception<code, code::duplicate_store>;
   using unknown_store = forge::exceptions::coded_exception<code, code::unknown_store>;
   using stopped = forge::exceptions::coded_exception<code, code::stopped>;
   using startup_failed = forge::exceptions::coded_exception<code, code::startup_failed>;
   using invalid_argument = forge::exceptions::coded_exception<code, code::invalid_argument>;
   using internal_error = forge::exceptions::coded_exception<code, code::internal_error>;
};

FORGE_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "forge.plugins.db.object")

} // namespace forge::plugins::db::object
