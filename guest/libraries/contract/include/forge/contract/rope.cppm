module;

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

export module forge.contract.rope;

import forge.contract.intrinsics;
import forge.contract.print;

export namespace forge::contract {

class rope {
 public:
   rope() = default;
   rope(const char* value) : _value(value == nullptr ? "" : value) {}
   rope(std::string_view value) : _value(value) {}

   template <std::size_t Size> void append(const char (&value)[Size]) {
      append(value, Size - 1U);
   }

   void append(const char* value, std::size_t size) {
      check(value != nullptr || size == 0U, "rope source is null");
      _value.append(value, size);
   }

   void append(const rope& value) {
      _value += value._value;
   }

   void append(rope&& value) {
      _value += value._value;
   }

   [[nodiscard]] char at(std::size_t index) const {
      check(index < _value.size(), "rope index is out of range");
      return _value[index];
   }

   [[nodiscard]] char operator[](std::size_t index) const {
      return at(index);
   }

   rope& operator+=(const char* value) {
      append(value, std::char_traits<char>::length(value));
      return *this;
   }

   rope& operator+=(const rope& value) {
      append(value);
      return *this;
   }

   rope& operator+=(rope&& value) {
      append(std::move(value));
      return *this;
   }

   friend rope operator+(rope left, const char* right) {
      return left += right;
   }

   friend rope operator+(rope left, const rope& right) {
      return left += right;
   }

   friend rope operator+(rope left, rope&& right) {
      return left += std::move(right);
   }

   [[nodiscard]] std::size_t length() const noexcept {
      return _value.size();
   }

   [[nodiscard]] const char* c_str() const noexcept {
      return _value.c_str();
   }

   [[nodiscard]] std::string_view sv() const noexcept {
      return _value;
   }

   void print() const {
      printl(_value.data(), _value.size());
   }

 private:
   std::string _value;
};

} // namespace forge::contract
