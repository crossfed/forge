module;

#include <string>

module forge.net.transport.endpoint;

namespace forge::net::transport {

std::string endpoint::authority() const {
   if (host_type == host_kind::ip6) {
      return "[" + host + "]:" + std::to_string(port);
   }
   return host + ":" + std::to_string(port);
}

} // namespace forge::net::transport
