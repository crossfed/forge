#include "details/auth_state_binding.hxx"

#include <algorithm>
#include <string_view>

namespace forge::auth::appauth::detail {
namespace {

[[nodiscard]] std::string hex_encode(std::string_view value) {
   constexpr auto hex = std::string_view{"0123456789abcdef"};

   auto result = std::string{};
   result.reserve(value.size() * 2U);
   for (const auto character : value) {
      const auto byte = static_cast<unsigned char>(character);
      result.push_back(hex[byte >> 4U]);
      result.push_back(hex[byte & 0x0fU]);
   }
   return result;
}

void append_field(std::string& target, std::string_view name, std::string_view value) {
   target.push_back('|');
   target.append(name);
   target.push_back('=');
   target.append(std::to_string(value.size()));
   target.push_back(':');
   target.append(hex_encode(value));
}

} // namespace

std::string canonical_auth_state_binding(const auth_state_config& config) {
   auto result = std::string{"forge-appauth-state-v1"};
   append_field(result, "issuer", config.issuer);
   append_field(result, "client_id", config.client_id);

   auto scopes = config.scopes;
   std::sort(scopes.begin(), scopes.end());
   result.append("|scope_count=");
   result.append(std::to_string(scopes.size()));
   for (const auto& scope : scopes) {
      append_field(result, "scope", scope);
   }

   result.append("|audience=");
   if (!config.audience.has_value()) {
      result.append("absent");
   } else {
      result.append("present");
      append_field(result, "audience_value", *config.audience);
   }
   return result;
}

} // namespace forge::auth::appauth::detail
