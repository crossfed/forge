module;

#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <forge/exceptions/macros.hpp>

module forge.net.http.cookie;

import forge.net.http.exceptions;
import forge.net.http.types;

namespace forge::net::http {
namespace {

[[nodiscard]] bool is_ctl(unsigned char value) noexcept {
   return value <= 0x1fU || value == 0x7fU;
}

[[nodiscard]] bool is_token_character(unsigned char value) noexcept {
   if ((value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')) {
      return true;
   }
   switch (value) {
   case '!':
   case '#':
   case '$':
   case '%':
   case '&':
   case '\'':
   case '*':
   case '+':
   case '-':
   case '.':
   case '^':
   case '_':
   case '`':
   case '|':
   case '~':
      return true;
   default:
      return false;
   }
}

[[nodiscard]] bool is_cookie_value_character(unsigned char value) noexcept {
   return value == 0x21U || (value >= 0x23U && value <= 0x2bU) || (value >= 0x2dU && value <= 0x3aU) ||
          (value >= 0x3cU && value <= 0x5bU) || (value >= 0x5dU && value <= 0x7eU);
}

[[nodiscard]] bool equal_ci(std::string_view left, std::string_view right) noexcept {
   if (left.size() != right.size()) {
      return false;
   }
   for (auto index = std::size_t{0}; index != left.size(); ++index) {
      auto value = static_cast<unsigned char>(left[index]);
      if (value >= 'A' && value <= 'Z') {
         value = static_cast<unsigned char>(value - 'A' + 'a');
      }
      auto expected = static_cast<unsigned char>(right[index]);
      if (expected >= 'A' && expected <= 'Z') {
         expected = static_cast<unsigned char>(expected - 'A' + 'a');
      }
      if (value != expected) {
         return false;
      }
   }
   return true;
}

[[nodiscard]] std::string_view trim_ows(std::string_view value) noexcept {
   while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
      value.remove_prefix(1U);
   }
   while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
      value.remove_suffix(1U);
   }
   return value;
}

void reject_controls(std::string_view value, std::string_view description) {
   for (const auto character : value) {
      if (is_ctl(static_cast<unsigned char>(character))) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, std::string{description} + " contains a control character");
      }
   }
}

void validate_name(std::string_view value) {
   if (value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie name must not be empty");
   }
   for (const auto character : value) {
      if (!is_token_character(static_cast<unsigned char>(character))) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie name is not an HTTP token");
      }
   }
}

void validate_value(std::string_view value) {
   for (const auto character : value) {
      if (!is_cookie_value_character(static_cast<unsigned char>(character))) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie value contains an invalid character");
      }
   }
}

void validate_path(std::string_view value) {
   if (value.empty() || value.front() != '/') {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie Path must start with /");
   }
   reject_controls(value, "cookie Path");
   if (value.find(';') != std::string_view::npos) {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie Path contains an invalid separator");
   }
}

void validate_domain(std::string_view value) {
   if (value.empty() || value.front() == '.' || value.back() == '.') {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie Domain is malformed");
   }
   auto previous_dot = true;
   auto label_length = std::size_t{0};
   auto label_ends_hyphen = false;
   for (const auto character : value) {
      const auto byte = static_cast<unsigned char>(character);
      if (byte == '.') {
         if (previous_dot || label_length == 0U || label_ends_hyphen) {
            FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie Domain is malformed");
         }
         previous_dot = true;
         label_length = 0;
         label_ends_hyphen = false;
         continue;
      }
      if (!((byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            byte == '-') ||
          (previous_dot && byte == '-')) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie Domain is malformed");
      }
      previous_dot = false;
      label_ends_hyphen = byte == '-';
      ++label_length;
      if (label_length > 63U) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie Domain label is too long");
      }
   }
   if (previous_dot || label_ends_hyphen) {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie Domain is malformed");
   }
}

void validate_cookie(const cookie& value) {
   validate_name(value.name);
   validate_value(value.value);
}

void validate_set_cookie(const set_cookie& value) {
   validate_cookie(cookie{.name = value.name, .value = value.value});
   if (value.path.has_value()) {
      validate_path(*value.path);
   }
   if (value.domain.has_value()) {
      validate_domain(*value.domain);
   }
   if (value.same_site_value.has_value()) {
      switch (*value.same_site_value) {
      case same_site::strict:
      case same_site::lax:
         break;
      case same_site::none:
         if (!value.secure) {
            FORGE_THROW_EXCEPTION(exceptions::bad_request, "SameSite=None requires Secure");
         }
         break;
      default:
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie SameSite value is invalid");
      }
   }
}

[[nodiscard]] std::pair<std::string_view, std::string_view> parse_pair(std::string_view value,
                                                                       std::string_view description) {
   value = trim_ows(value);
   const auto separator = value.find('=');
   if (separator == std::string_view::npos) {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, std::string{description} + " requires name=value");
   }
   auto name = trim_ows(value.substr(0, separator));
   auto item = value.substr(separator + 1U);
   validate_name(name);
   validate_value(item);
   return {name, item};
}

[[nodiscard]] std::optional<same_site> parse_same_site(std::string_view value) {
   if (equal_ci(value, "Strict")) {
      return same_site::strict;
   }
   if (equal_ci(value, "Lax")) {
      return same_site::lax;
   }
   if (equal_ci(value, "None")) {
      return same_site::none;
   }
   FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie SameSite is malformed");
}

[[nodiscard]] std::chrono::seconds parse_max_age(std::string_view value) {
   if (value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie Max-Age is empty");
   }
   auto parsed = std::int64_t{};
   const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
   if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie Max-Age is malformed");
   }
   if constexpr (sizeof(std::chrono::seconds::rep) < sizeof(parsed)) {
      if (parsed < std::numeric_limits<std::chrono::seconds::rep>::min() ||
          parsed > std::numeric_limits<std::chrono::seconds::rep>::max()) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie Max-Age is out of range");
      }
   }
   return std::chrono::seconds{static_cast<std::chrono::seconds::rep>(parsed)};
}

[[nodiscard]] std::string_view same_site_text(same_site value) {
   switch (value) {
   case same_site::strict:
      return "Strict";
   case same_site::lax:
      return "Lax";
   case same_site::none:
      return "None";
   default:
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "cookie SameSite value is invalid");
   }
}

} // namespace

std::vector<cookie> parse_cookie_header(std::string_view value) {
   reject_controls(value, "Cookie header");
   if (value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "Cookie header must not be empty");
   }
   auto result = std::vector<cookie>{};
   while (!value.empty()) {
      const auto separator = value.find(';');
      const auto item = separator == std::string_view::npos ? value : value.substr(0, separator);
      const auto [name, item_value] = parse_pair(item, "Cookie header");
      for (const auto& existing : result) {
         if (existing.name == name) {
            FORGE_THROW_EXCEPTION(exceptions::bad_request, "Cookie header contains a duplicate name");
         }
      }
      result.push_back(cookie{.name = std::string{name}, .value = std::string{item_value}});
      if (separator == std::string_view::npos) {
         break;
      }
      value.remove_prefix(separator + 1U);
      if (value.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "Cookie header has an empty item");
      }
   }
   return result;
}

std::string format_cookie_header(std::span<const cookie> values) {
   auto result = std::string{};
   for (const auto& value : values) {
      validate_cookie(value);
      for (const auto& existing : values) {
         if (&existing != &value && existing.name == value.name) {
            FORGE_THROW_EXCEPTION(exceptions::bad_request, "Cookie header contains a duplicate name");
         }
      }
      if (!result.empty()) {
         result += "; ";
      }
      result += value.name;
      result += '=';
      result += value.value;
   }
   return result;
}

set_cookie parse_set_cookie_header(std::string_view value) {
   reject_controls(value, "Set-Cookie header");
   const auto first_separator = value.find(';');
   const auto first = first_separator == std::string_view::npos ? value : value.substr(0, first_separator);
   const auto [name, cookie_value] = parse_pair(first, "Set-Cookie header");
   auto result = set_cookie{.name = std::string{name}, .value = std::string{cookie_value}};

   auto seen_secure = false;
   auto seen_http_only = false;
   auto seen_path = false;
   auto seen_domain = false;
   auto seen_max_age = false;
   auto seen_same_site = false;
   if (first_separator == std::string_view::npos) {
      return result;
   }
   value.remove_prefix(first_separator + 1U);
   if (value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie header has an empty attribute");
   }
   while (!value.empty()) {
      const auto separator = value.find(';');
      const auto item = trim_ows(separator == std::string_view::npos ? value : value.substr(0, separator));
      if (item.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie header has an empty attribute");
      }
      const auto equals = item.find('=');
      const auto attribute = trim_ows(item.substr(0, equals));
      const auto attribute_value =
          equals == std::string_view::npos ? std::string_view{} : trim_ows(item.substr(equals + 1U));
      if (attribute.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie attribute is malformed");
      }
      if (equal_ci(attribute, "Secure")) {
         if (equals != std::string_view::npos || seen_secure) {
            FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie Secure attribute is ambiguous");
         }
         seen_secure = true;
         result.secure = true;
      } else if (equal_ci(attribute, "HttpOnly")) {
         if (equals != std::string_view::npos || seen_http_only) {
            FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie HttpOnly attribute is ambiguous");
         }
         seen_http_only = true;
         result.http_only = true;
      } else if (equal_ci(attribute, "Path")) {
         if (equals == std::string_view::npos || seen_path) {
            FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie Path attribute is ambiguous");
         }
         validate_path(attribute_value);
         seen_path = true;
         result.path = std::string{attribute_value};
      } else if (equal_ci(attribute, "Domain")) {
         if (equals == std::string_view::npos || seen_domain) {
            FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie Domain attribute is ambiguous");
         }
         validate_domain(attribute_value);
         seen_domain = true;
         result.domain = std::string{attribute_value};
      } else if (equal_ci(attribute, "Max-Age")) {
         if (equals == std::string_view::npos || seen_max_age) {
            FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie Max-Age attribute is ambiguous");
         }
         seen_max_age = true;
         result.max_age = parse_max_age(attribute_value);
      } else if (equal_ci(attribute, "SameSite")) {
         if (equals == std::string_view::npos || seen_same_site) {
            FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie SameSite attribute is ambiguous");
         }
         seen_same_site = true;
         result.same_site_value = parse_same_site(attribute_value);
      } else {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie attribute is not supported");
      }
      if (separator == std::string_view::npos) {
         break;
      }
      value.remove_prefix(separator + 1U);
      if (value.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "Set-Cookie header has an empty attribute");
      }
   }
   validate_set_cookie(result);
   return result;
}

std::string format_set_cookie(const set_cookie& value) {
   validate_set_cookie(value);
   auto result = value.name + "=" + value.value;
   if (value.path.has_value()) {
      result += "; Path=" + *value.path;
   }
   if (value.domain.has_value()) {
      result += "; Domain=" + *value.domain;
   }
   if (value.max_age.has_value()) {
      result += "; Max-Age=" + std::to_string(value.max_age->count());
   }
   if (value.secure) {
      result += "; Secure";
   }
   if (value.http_only) {
      result += "; HttpOnly";
   }
   if (value.same_site_value.has_value()) {
      result += "; SameSite=" + std::string{same_site_text(*value.same_site_value)};
   }
   return result;
}

void append_set_cookie(response& response_value, const set_cookie& value) {
   response_value.insert("Set-Cookie", format_set_cookie(value));
}

} // namespace forge::net::http
