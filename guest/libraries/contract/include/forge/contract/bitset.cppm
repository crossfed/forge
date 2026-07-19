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

   [[nodiscard]] std::size_t num_blocks() const noexcept {
      return _bits.size();
   }

   void resize(size_type bits) {
      _bits.resize(calc_num_blocks(bits), 0U);
      _size = bits;
      zero_unused_bits();
   }

   void set(size_type position) {
      require_position(position);
      _bits[block_index(position)] |= bit_mask(position);
   }

   void clear(size_type position) {
      require_position(position);
      _bits[block_index(position)] &= static_cast<std::uint8_t>(~bit_mask(position));
   }

   [[nodiscard]] bool test(size_type position) const {
      require_position(position);
      return (_bits[block_index(position)] & bit_mask(position)) != 0U;
   }

   [[nodiscard]] bool operator[](size_type position) const {
      return test(position);
   }

   void flip(size_type position) {
      require_position(position);
      _bits[block_index(position)] ^= bit_mask(position);
   }

   void flip() {
      for (auto& value : _bits) {
         value = static_cast<std::uint8_t>(~value);
      }
      zero_unused_bits();
   }

   [[nodiscard]] bool all() const {
      for (auto position = size_type{}; position < _size; ++position) {
         if (!test(position)) {
            return false;
         }
      }
      return true;
   }

   [[nodiscard]] bool none() const noexcept {
      for (const auto value : _bits) {
         if (value != 0U) {
            return false;
         }
      }
      return true;
   }

   void zero_all_bits() noexcept {
      for (auto& value : _bits) {
         value = 0U;
      }
   }

   bitset& operator|=(const bitset& other) {
      check(size() == other.size(), "bitset sizes differ");
      for (auto index = std::size_t{}; index < _bits.size(); ++index) {
         _bits[index] |= other._bits[index];
      }
      return *this;
   }

   [[nodiscard]] std::uint8_t& byte(std::size_t index) {
      check(index < _bits.size(), "bitset byte index is out of range");
      return _bits[index];
   }

   [[nodiscard]] const std::uint8_t& byte(std::size_t index) const {
      check(index < _bits.size(), "bitset byte index is out of range");
      return _bits[index];
   }

   [[nodiscard]] std::string to_string() const {
      auto result = std::string(_size, '0');
      for (auto index = size_type{}; index < _size; ++index) {
         result[_size - index - 1U] = test(index) ? '1' : '0';
      }
      return result;
   }

   [[nodiscard]] static bitset from_string(std::string_view value) {
      check(value.size() <= static_cast<std::size_t>(npos), "bitset is too large");
      auto result = bitset{};
      result.resize(static_cast<size_type>(value.size()));
      for (auto index = std::size_t{}; index < value.size(); ++index) {
         check(value[index] == '0' || value[index] == '1', "unexpected character in bitset string representation");
         if (value[index] == '1') {
            result.set(static_cast<size_type>(value.size() - index - 1U));
         }
      }
      return result;
   }

   void print() const {
      const auto value = to_string();
      if (!value.empty()) {
         printl(value.data(), value.size());
      }
   }

   friend bool operator==(const bitset&, const bitset&) = default;
   friend auto operator<=>(const bitset& left, const bitset& right) {
      if (const auto result = left._size <=> right._size; result != 0) {
         return result;
      }
      return left._bits <=> right._bits;
   }

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

   void require_position(size_type position) const {
      check(position < _size, "bitset position is out of range");
   }

   void zero_unused_bits() noexcept {
      const auto used = _size % bits_per_block;
      if (used != 0U && !_bits.empty()) {
         _bits.back() &= static_cast<std::uint8_t>((1U << used) - 1U);
      }
   }

   size_type _size = 0U;
   buffer_type _bits;
};

} // namespace forge::contract
