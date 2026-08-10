module;

#include <cstddef>
#include <cstdint>
#include <openssl/crypto.h>
#include <span>
#include <string>
#include <utility>

module forge.crypto.core.secret_bytes;

namespace forge::crypto::core {

void secure_erase(std::span<std::uint8_t> value) noexcept {
   if (!value.empty()) {
      OPENSSL_cleanse(value.data(), value.size());
   }
}

void secure_erase(bytes& value) noexcept {
   secure_erase(std::span<std::uint8_t>{value.data(), value.size()});
   value.clear();
   value.shrink_to_fit();
}

void secure_erase(std::string& value) noexcept {
   secure_erase(std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(value.data()), value.size()});
   value.clear();
   value.shrink_to_fit();
}

secret_bytes::secret_bytes(bytes value) : value_{std::move(value)} {}

secret_bytes::secret_bytes(std::span<const std::uint8_t> value) : value_{value.begin(), value.end()} {}

secret_bytes::~secret_bytes() {
   clear();
}

secret_bytes::secret_bytes(secret_bytes&& other) noexcept : value_{std::move(other.value_)} {
   other.value_.clear();
}

secret_bytes& secret_bytes::operator=(secret_bytes&& other) noexcept {
   if (this != &other) {
      clear();
      value_ = std::move(other.value_);
      other.value_.clear();
   }
   return *this;
}

bool secret_bytes::empty() const noexcept {
   return value_.empty();
}

std::size_t secret_bytes::size() const noexcept {
   return value_.size();
}

std::span<const std::uint8_t> secret_bytes::span() const noexcept {
   return std::span<const std::uint8_t>{value_.data(), value_.size()};
}

bytes secret_bytes::copy() const {
   return value_;
}

void secret_bytes::assign(bytes value) {
   clear();
   value_ = std::move(value);
}

void secret_bytes::clear() noexcept {
   secure_erase(value_);
}

} // namespace forge::crypto::core
