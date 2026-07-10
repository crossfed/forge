module;

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

export module forge.net.http.route_context;

import forge.asio.runtime;
import forge.net.http.target;
import forge.net.http.types;

export namespace forge::net::http {

struct route_context {
   const request& request;
   target parsed_target;
   std::unordered_map<std::string, std::string> route_params;
   forge::asio::runtime* runtime = nullptr;

   [[nodiscard]] std::optional<std::string_view> route_param(std::string_view name) const;
};

route_context make_route_context(const request& request);

} // namespace forge::net::http
