module;

#include <forge/exceptions/macros.hpp>

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

void stream::impl::require_capacity(std::size_t size) const {
   const auto available_in_block = block_offset_ < block_size ? block_size - block_offset_ : 0U;
   if (size <= available_in_block) {
      return;
   }

   const auto remaining = size - available_in_block;
   auto required_blocks = remaining / block_size;
   if (remaining % block_size != 0U) {
      ++required_blocks;
   }

   if (required_blocks > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()) || exhausted_) {
      FORGE_THROW_EXCEPTION(exceptions::counter_exhausted, "XSalsa20 stream counter is exhausted");
   }

   const auto blocks = static_cast<std::uint64_t>(required_blocks);
   const auto last_counter = std::numeric_limits<std::uint64_t>::max();
   if (next_block_counter_ > last_counter - (blocks - 1U)) {
      FORGE_THROW_EXCEPTION(exceptions::counter_exhausted, "XSalsa20 stream counter is exhausted");
   }
}

void stream::impl::load_block() {
   if (exhausted_) {
      FORGE_THROW_EXCEPTION(exceptions::counter_exhausted, "XSalsa20 stream counter is exhausted");
   }

   std::fill(keystream_.begin(), keystream_.end(), std::uint8_t{0});
   if (forge_xsalsa20_vendor_xor_ic(keystream_.data(), keystream_.data(),
                                    static_cast<unsigned long long>(block_size), nonce_.bytes.data(),
                                    next_block_counter_, key_.span().data()) != 0) {
      FORGE_THROW_EXCEPTION(exceptions::backend_error, "XSalsa20 backend rejected a block transform");
   }

   block_offset_ = 0;
   if (next_block_counter_ == std::numeric_limits<std::uint64_t>::max()) {
      exhausted_ = true;
   } else {
      ++next_block_counter_;
   }
}

void stream::impl::transform(std::span<std::uint8_t> bytes) {
   require_capacity(bytes.size());

   auto offset = std::size_t{0};
   while (offset < bytes.size()) {
      if (block_offset_ == block_size) {
         load_block();
      }

      const auto chunk_size = std::min(bytes.size() - offset, block_size - block_offset_);
      for (auto index = std::size_t{0}; index < chunk_size; ++index) {
         bytes[offset + index] ^= keystream_[block_offset_ + index];
      }
      offset += chunk_size;
      block_offset_ += chunk_size;
   }
}

} // namespace forge::crypto::symmetric::xsalsa20
