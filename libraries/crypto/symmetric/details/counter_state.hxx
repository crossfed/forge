#pragma once

#include <cstddef>
#include <cstdint>

namespace forge::crypto::symmetric::xsalsa20::detail {

class counter_state {
 public:
   counter_state() noexcept;
   counter_state(std::uint64_t next_block_counter, std::size_t block_offset, bool exhausted) noexcept;

   [[nodiscard]] std::uint64_t next_block_counter() const noexcept;
   [[nodiscard]] std::size_t block_offset() const noexcept;
   [[nodiscard]] bool exhausted() const noexcept;
   [[nodiscard]] bool requires_block() const noexcept;
   [[nodiscard]] std::size_t available_in_block() const noexcept;

   void require_capacity(std::size_t size) const;
   void commit_loaded_block() noexcept;
   void consume(std::size_t size) noexcept;

 private:
   std::uint64_t next_block_counter_ = 0;
   std::size_t block_offset_ = 0;
   bool exhausted_ = false;
};

} // namespace forge::crypto::symmetric::xsalsa20::detail
