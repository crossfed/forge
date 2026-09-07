module;

#include <forge/exceptions/macros.hpp>

#include "details/counter_state.hxx"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

module forge.crypto.symmetric.xsalsa20;

#include "details/stream_impl.hxx"

namespace forge::crypto::symmetric::xsalsa20 {

key::key(std::span<const std::uint8_t> bytes) {
   if (bytes.size() != key_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "XSalsa20 requires a 32-byte key");
   }
   value_.assign(core::bytes{bytes.begin(), bytes.end()});
}

key::~key() = default;
key::key(key&&) noexcept = default;
key& key::operator=(key&&) noexcept = default;

std::span<const std::uint8_t> key::span() const & noexcept {
   return value_.span();
}

nonce make_nonce(std::span<const std::uint8_t> bytes) {
   if (bytes.size() != nonce_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_nonce, "XSalsa20 requires a 24-byte nonce");
   }

   auto result = nonce{};
   std::copy(bytes.begin(), bytes.end(), result.bytes.begin());
   return result;
}

std::unique_ptr<stream::impl> stream::make_impl(const xsalsa20::key& key, xsalsa20::nonce nonce) {
   if (key.span().size() != key_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "XSalsa20 key has no usable secret material");
   }
   return std::make_unique<impl>(core::secret_bytes{key.span()}, nonce);
}

stream::stream(const xsalsa20::key& key, xsalsa20::nonce nonce) : impl_{make_impl(key, nonce)} {}

stream::stream(std::unique_ptr<impl> implementation) noexcept : impl_{std::move(implementation)} {}

stream::~stream() = default;
stream::stream(stream&&) noexcept = default;
stream& stream::operator=(stream&&) noexcept = default;

void stream::transform(std::span<std::uint8_t> bytes) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state, "XSalsa20 stream has been moved from");
   }
   impl_->transform(bytes);
}

core::bytes transform(const xsalsa20::key& key, const xsalsa20::nonce& nonce, std::span<const std::uint8_t> input) {
   auto output = core::bytes{input.begin(), input.end()};
   auto cipher = stream{key, nonce};
   cipher.transform(output);
   return output;
}

} // namespace forge::crypto::symmetric::xsalsa20
