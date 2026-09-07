#include <forge/exceptions/macros.hpp>

#include "details/counter_state.hxx"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

import forge.crypto.symmetric.xsalsa20;

namespace forge::crypto::symmetric::xsalsa20::detail {

static_assert(counter_block_size == block_size);

bool requires_block(const counter_position& position) noexcept {
   return position.block_offset == counter_block_size;
}

std::size_t available_in_block(const counter_position& position) noexcept {
   return requires_block(position) ? 0U : counter_block_size - position.block_offset;
}

void require_capacity(const counter_position& position, std::size_t size) {
   const auto available = available_in_block(position);
   if (size <= available) {
      return;
   }

   const auto remaining = size - available;
   auto required_blocks = remaining / counter_block_size;
   if (remaining % counter_block_size != 0U) {
      ++required_blocks;
   }

   if (required_blocks > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()) || position.exhausted) {
      FORGE_THROW_EXCEPTION(exceptions::counter_exhausted, "XSalsa20 stream counter is exhausted");
   }

   const auto blocks = static_cast<std::uint64_t>(required_blocks);
   const auto last_counter = std::numeric_limits<std::uint64_t>::max();
   if (position.next_block_counter > last_counter - (blocks - 1U)) {
      FORGE_THROW_EXCEPTION(exceptions::counter_exhausted, "XSalsa20 stream counter is exhausted");
   }
}

counter_position advance_after_block_load(counter_position position) noexcept {
   assert(!position.exhausted);
   assert(requires_block(position));

   position.block_offset = 0;
   if (position.next_block_counter == std::numeric_limits<std::uint64_t>::max()) {
      position.exhausted = true;
   } else {
      ++position.next_block_counter;
   }
   return position;
}

counter_position advance_after_consume(counter_position position, std::size_t size) noexcept {
   assert(size <= available_in_block(position));
   position.block_offset += size;
   return position;
}

counter_state::counter_state() noexcept = default;

std::uint64_t counter_state::next_block_counter() const noexcept {
   return position_.next_block_counter;
}

std::size_t counter_state::block_offset() const noexcept {
   return position_.block_offset;
}

bool counter_state::exhausted() const noexcept {
   return position_.exhausted;
}

bool counter_state::requires_block() const noexcept {
   return detail::requires_block(position_);
}

std::size_t counter_state::available_in_block() const noexcept {
   return detail::available_in_block(position_);
}

void counter_state::require_capacity(std::size_t size) const {
   detail::require_capacity(position_, size);
}

void counter_state::commit_loaded_block() noexcept {
   position_ = detail::advance_after_block_load(position_);
}

void counter_state::consume(std::size_t size) noexcept {
   position_ = detail::advance_after_consume(position_, size);
}

} // namespace forge::crypto::symmetric::xsalsa20::detail
