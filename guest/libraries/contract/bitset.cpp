module;

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

module forge.contract.bitset;

import forge.contract.intrinsics;
import forge.contract.print;

namespace forge::contract {

std::size_t bitset::num_blocks() const noexcept {
   return _bits.size();
}

void bitset::resize(size_type bits) {
   _bits.resize(calc_num_blocks(bits), 0U);
   _size = bits;
   zero_unused_bits();
}

void bitset::set(size_type position) {
   require_position(position);
   _bits[block_index(position)] |= bit_mask(position);
}

void bitset::clear(size_type position) {
   require_position(position);
   _bits[block_index(position)] &= static_cast<std::uint8_t>(~bit_mask(position));
}

bool bitset::test(size_type position) const {
   require_position(position);
   return (_bits[block_index(position)] & bit_mask(position)) != 0U;
}

bool bitset::operator[](size_type position) const {
   return test(position);
}

void bitset::flip(size_type position) {
   require_position(position);
   _bits[block_index(position)] ^= bit_mask(position);
}

void bitset::flip() {
   for (auto& value : _bits) {
      value = static_cast<std::uint8_t>(~value);
   }
   zero_unused_bits();
}

bool bitset::all() const {
   for (auto position = size_type{}; position < _size; ++position) {
      if (!test(position)) {
         return false;
      }
   }
   return true;
}

bool bitset::none() const noexcept {
   for (const auto value : _bits) {
      if (value != 0U) {
         return false;
      }
   }
   return true;
}

void bitset::zero_all_bits() noexcept {
   for (auto& value : _bits) {
      value = 0U;
   }
}

bitset& bitset::operator|=(const bitset& other) {
   check(size() == other.size(), "bitset sizes differ");
   for (auto index = std::size_t{}; index < _bits.size(); ++index) {
      _bits[index] |= other._bits[index];
   }
   return *this;
}

std::uint8_t& bitset::byte(std::size_t index) {
   check(index < _bits.size(), "bitset byte index is out of range");
   return _bits[index];
}

const std::uint8_t& bitset::byte(std::size_t index) const {
   check(index < _bits.size(), "bitset byte index is out of range");
   return _bits[index];
}

std::string bitset::to_string() const {
   auto result = std::string(_size, '0');
   for (auto index = size_type{}; index < _size; ++index) {
      result[_size - index - 1U] = test(index) ? '1' : '0';
   }
   return result;
}

bitset bitset::from_string(std::string_view value) {
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

void bitset::print() const {
   const auto value = to_string();
   if (!value.empty()) {
      printl(value.data(), value.size());
   }
}

std::strong_ordering operator<=>(const bitset& left, const bitset& right) {
   if (const auto result = left._size <=> right._size; result != 0) {
      return result;
   }
   for (auto index = std::size_t{}; index < left._bits.size(); ++index) {
      if (left._bits[index] < right._bits[index]) {
         return std::strong_ordering::less;
      }
      if (left._bits[index] > right._bits[index]) {
         return std::strong_ordering::greater;
      }
   }
   return std::strong_ordering::equal;
}

void bitset::require_position(size_type position) const {
   check(position < _size, "bitset position is out of range");
}

void bitset::zero_unused_bits() noexcept {
   const auto used = _size % bits_per_block;
   if (used != 0U && !_bits.empty()) {
      _bits.back() &= static_cast<std::uint8_t>((1U << used) - 1U);
   }
}

} // namespace forge::contract
