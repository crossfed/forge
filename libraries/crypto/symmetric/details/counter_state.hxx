#pragma once

#include <cstddef>
#include <cstdint>

namespace forge::crypto::symmetric::xsalsa20::detail {

inline constexpr auto counter_block_size = std::size_t{64};

struct counter_position {
   std::uint64_t next_block_counter = 0;
   std::size_t block_offset = counter_block_size;
   bool exhausted = false;
};

[[nodiscard]] bool requires_block(const counter_position& position) noexcept;
[[nodiscard]] std::size_t available_in_block(const counter_position& position) noexcept;
void require_capacity(const counter_position& position, std::size_t size);
[[nodiscard]] counter_position advance_after_block_load(counter_position position) noexcept;
[[nodiscard]] counter_position advance_after_consume(counter_position position, std::size_t size) noexcept;

class counter_state {
 public:
   counter_state() noexcept;

   [[nodiscard]] std::uint64_t next_block_counter() const noexcept;
   [[nodiscard]] std::size_t block_offset() const noexcept;
   [[nodiscard]] bool exhausted() const noexcept;
   [[nodiscard]] bool requires_block() const noexcept;
   [[nodiscard]] std::size_t available_in_block() const noexcept;

   void require_capacity(std::size_t size) const;
   void commit_loaded_block() noexcept;
   void consume(std::size_t size) noexcept;

 private:
   counter_position position_;
};

} // namespace forge::crypto::symmetric::xsalsa20::detail
