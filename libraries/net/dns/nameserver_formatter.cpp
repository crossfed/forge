#include <string>

#include "details/nameserver_formatter.hxx"

namespace forge::net::dns::detail {

std::string format_nameserver_csv(std::span<const nameserver_format_view> servers) {
   auto out = std::string{};
   for (const auto& server : servers) {
      if (!out.empty()) {
         out.push_back(',');
      }
      if (server.ipv6) {
         out.append("[");
         out.append(server.address);
         out.append("]:");
      } else {
         out.append(server.address);
         out.push_back(':');
      }
      out.append(std::to_string(server.port));
      if (server.ipv6 && !server.scope_id.empty()) {
         out.push_back('%');
         out.append(server.scope_id);
      }
   }
   return out;
}

} // namespace forge::net::dns::detail
