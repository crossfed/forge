#include <forge/exceptions/macros.hpp>

#include "details/counter_state.hxx"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

import forge.crypto.symmetric.xsalsa20;

namespace forge::crypto::symmetric::xsalsa20::detail {

counter_state::counter_state() noexcept : block_offset_{block_size} {}

counter_state::counter_state(std::uint64_t next_block_counter, std::size_t block_offset, bool exhausted) noexcept
    : next_block_counter_{next_block_counter}, block_offset_{block_offset}, exhausted_{exhausted} {
   assert(block_offset_ <= block_size);
}

std::uint64_t counter_state::next_block_counter() const noexcept {
   return next_block_counter_;
}

std::size_t counter_state::block_offset() const noexcept {
   return block_offset_;
}

bool counter_state::exhausted() const noexcept {
   return exhausted_;
}

bool counter_state::requires_block() const noexcept {
   return block_offset_ == block_size;
}

std::size_t counter_state::available_in_block() const noexcept {
   return requires_block() ? 0U : block_size - block_offset_;
}

void counter_state::require_capacity(std::size_t size) const {
   const auto available = available_in_block();
   if (size <= available) {
      return;
   }

   const auto remaining = size - available;
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

void counter_state::commit_loaded_block() noexcept {
   assert(!exhausted_);
   assert(requires_block());

   block_offset_ = 0;
   if (next_block_counter_ == std::numeric_limits<std::uint64_t>::max()) {
      exhausted_ = true;
   } else {
      ++next_block_counter_;
   }
}

void counter_state::consume(std::size_t size) noexcept {
   assert(size <= available_in_block());
   block_offset_ += size;
}

} // namespace forge::crypto::symmetric::xsalsa20::detail
