module;

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module forge.contract.bitset;

import forge.contract.intrinsics;
import forge.contract.print;
import forge.contract.varint;
import forge.raw.codec;

export namespace forge::contract {

class bitset {
 public:
   using buffer_type = std::vector<std::uint8_t>;
   using size_type = std::uint32_t;
   static constexpr size_type bits_per_block = 8U;
   static constexpr size_type npos = static_cast<size_type>(-1);

   [[nodiscard]] static constexpr size_type calc_num_blocks(size_type bits) noexcept {
      return (bits + bits_per_block - 1U) / bits_per_block;
   }

   [[nodiscard]] constexpr size_type size() const noexcept {
      return _size;
   }

   [[nodiscard]] std::size_t num_blocks() const noexcept;
   void resize(size_type bits);
   void set(size_type position);
   void clear(size_type position);
   [[nodiscard]] bool test(size_type position) const;
   [[nodiscard]] bool operator[](size_type position) const;
   void flip(size_type position);
   void flip();
   [[nodiscard]] bool all() const;
   [[nodiscard]] bool none() const noexcept;
   void zero_all_bits() noexcept;
   bitset& operator|=(const bitset& other);
   [[nodiscard]] std::uint8_t& byte(std::size_t index);
   [[nodiscard]] const std::uint8_t& byte(std::size_t index) const;
   [[nodiscard]] std::string to_string() const;
   [[nodiscard]] static bitset from_string(std::string_view value);
   void print() const;

   friend bool operator==(const bitset&, const bitset&) = default;
   friend std::strong_ordering operator<=>(const bitset& left, const bitset& right);

   template <typename Stream> friend void raw_pack(Stream& stream, const bitset& value) {
      ::forge::raw::pack(stream, unsigned_int{value._size});
      for (const auto byte : value._bits) {
         ::forge::raw::pack(stream, byte);
      }
   }

   template <typename Stream> friend void raw_unpack(Stream& stream, bitset& value) {
      auto bits = unsigned_int{};
      ::forge::raw::unpack(stream, bits);
      value.resize(bits.value);
      for (auto& byte : value._bits) {
         ::forge::raw::unpack(stream, byte);
      }
      value.zero_unused_bits();
   }

 private:
   [[nodiscard]] static constexpr size_type block_index(size_type position) noexcept {
      return position / bits_per_block;
   }

   [[nodiscard]] static constexpr std::uint8_t bit_mask(size_type position) noexcept {
      return static_cast<std::uint8_t>(1U << (position % bits_per_block));
   }

   void require_position(size_type position) const;
   void zero_unused_bits() noexcept;

   size_type _size = 0U;
   buffer_type _bits;
};

} // namespace forge::contract
