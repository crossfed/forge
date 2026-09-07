module;

#include <forge/exceptions/macros.hpp>

#include "details/counter_state.hxx"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

module forge.crypto.symmetric.xsalsa20;

#include "details/stream_impl.hxx"

namespace {

extern "C" int forge_xsalsa20_vendor_xor_ic(unsigned char* output, const unsigned char* input,
                                             unsigned long long size, const unsigned char* nonce,
                                             std::uint64_t initial_block_counter, const unsigned char* key);

} // namespace

namespace forge::crypto::symmetric::xsalsa20 {

static_assert(block_size <= std::numeric_limits<unsigned long long>::max());

stream::impl::impl(core::secret_bytes key, xsalsa20::nonce nonce) noexcept
    : key_{std::move(key)}, nonce_{nonce} {}

stream::impl::~impl() {
   core::secure_erase(std::span<std::uint8_t>{keystream_});
   key_.clear();
}

void stream::impl::load_block() {
   auto generated = std::array<std::uint8_t, block_size>{};
   if (forge_xsalsa20_vendor_xor_ic(generated.data(), generated.data(),
                                    static_cast<unsigned long long>(block_size), nonce_.bytes.data(),
                                    position_.next_block_counter(), key_.span().data()) != 0) {
      core::secure_erase(std::span<std::uint8_t>{generated});
      FORGE_THROW_EXCEPTION(exceptions::backend_error, "XSalsa20 backend rejected a block transform");
   }

   keystream_ = generated;
   core::secure_erase(std::span<std::uint8_t>{generated});
   position_.commit_loaded_block();
}

void stream::impl::transform(std::span<std::uint8_t> bytes) {
   position_.require_capacity(bytes.size());

   auto offset = std::size_t{0};
   while (offset < bytes.size()) {
      if (position_.requires_block()) {
         load_block();
      }

      const auto chunk_size = std::min(bytes.size() - offset, position_.available_in_block());
      for (auto index = std::size_t{0}; index < chunk_size; ++index) {
         bytes[offset + index] ^= keystream_[position_.block_offset() + index];
      }
      offset += chunk_size;
      position_.consume(chunk_size);
   }
}

} // namespace forge::crypto::symmetric::xsalsa20
