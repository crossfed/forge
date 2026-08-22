module;

#include <cstdint>
#include <span>

#include <openssl/crypto.h>

module forge.crypto.core.constant_time;

namespace forge::crypto::core {

bool constant_time_equal(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right) noexcept {
   return left.size() == right.size() && CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

} // namespace forge::crypto::core
