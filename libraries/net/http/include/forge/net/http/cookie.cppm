module;

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module forge.net.http.cookie;

import forge.net.http.types;

export namespace forge::net::http {

struct cookie {
   std::string name;
   std::string value;
};

enum class same_site {
   strict,
   lax,
   none,
};

struct set_cookie {
   std::string name;
   std::string value;
   std::optional<std::string> path;
   std::optional<std::string> domain;
   std::optional<std::chrono::seconds> max_age;
   std::optional<same_site> same_site_value;
   bool secure = false;
   bool http_only = false;
};

[[nodiscard]] std::vector<cookie> parse_cookie_header(std::string_view value);
[[nodiscard]] std::string format_cookie_header(std::span<const cookie> values);
[[nodiscard]] set_cookie parse_set_cookie_header(std::string_view value);
[[nodiscard]] std::string format_set_cookie(const set_cookie& value);
void append_set_cookie(response& response_value, const set_cookie& value);

} // namespace forge::net::http
