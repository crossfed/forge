module;

#include <ares.h>

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <mutex>

module forge.net.dns.resolver;

#include "details/cares_library.hxx"

namespace forge::net::dns {

std::shared_ptr<cares_library> cares_library::acquire() {
   auto& global = state();
   auto lock = std::scoped_lock{global.state_mutex};
   if (auto active = global.active.lock()) {
      return active;
   }
   if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS) {
      FORGE_THROW_EXCEPTION(exceptions::internal, "c-ares global initialization failed");
   }
   try {
      auto active = std::shared_ptr<cares_library>{new cares_library{}};
      global.active = active;
      return active;
   } catch (...) {
      ares_library_cleanup();
      throw;
   }
}

cares_library::~cares_library() {
   auto& global = state();
   auto lock = std::scoped_lock{global.state_mutex};
   global.active.reset();
   ares_library_cleanup();
}

cares_library::global_state& cares_library::state() {
   static auto value = global_state{};
   return value;
}

} // namespace forge::net::dns
