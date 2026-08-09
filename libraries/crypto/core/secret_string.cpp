module;

#include <cstddef>
#include <string>
#include <string_view>

module forge.crypto.core.secret_string;

import forge.crypto.core.secret_bytes;

namespace forge::crypto::core {
secret_string::secret_string(const std::string& value) : value_{value} {}

secret_string::secret_string(std::string&& value) noexcept {
   value_.swap(value);
   secure_erase(value);
}

secret_string::secret_string(const char* value) : value_{value == nullptr ? "" : value} {}

secret_string::~secret_string() {
   clear();
}

secret_string::secret_string(const secret_string& other) : value_{other.value_} {}

secret_string::secret_string(secret_string&& other) noexcept {
   value_.swap(other.value_);
   other.clear();
}

secret_string& secret_string::operator=(const secret_string& other) {
   if (this != &other) {
      auto replacement = secret_string{other};
      clear();
      value_.swap(replacement.value_);
   }
   return *this;
}

secret_string& secret_string::operator=(secret_string&& other) noexcept {
   if (this != &other) {
      clear();
      value_.swap(other.value_);
      other.clear();
   }
   return *this;
}

secret_string& secret_string::operator=(const std::string& value) {
   auto replacement = secret_string{value};
   clear();
   value_.swap(replacement.value_);
   return *this;
}

secret_string& secret_string::operator=(std::string&& value) noexcept {
   clear();
   value_.swap(value);
   secure_erase(value);
   return *this;
}

secret_string& secret_string::operator=(const char* value) {
   return *this = std::string{value == nullptr ? "" : value};
}

bool secret_string::empty() const noexcept {
   return value_.empty();
}

std::size_t secret_string::size() const noexcept {
   return value_.size();
}

const char* secret_string::data() const noexcept {
   return value_.data();
}

std::string_view secret_string::view() const noexcept {
   return value_;
}

secret_string::operator std::string_view() const noexcept {
   return view();
}

void secret_string::clear() noexcept {
   secure_erase(value_);
}

} // namespace forge::crypto::core
