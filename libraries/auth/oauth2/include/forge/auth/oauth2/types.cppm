module;

#include <chrono>
#include <optional>
#include <string>
#include <vector>

export module forge.auth.oauth2.types;

export namespace forge::auth::oauth2 {

using scope_set = std::vector<std::string>;
using time_point = std::chrono::system_clock::time_point;

struct token_request {
   scope_set scopes;
   std::optional<std::string> audience;
   std::chrono::seconds minimum_validity{};
};

} // namespace forge::auth::oauth2
