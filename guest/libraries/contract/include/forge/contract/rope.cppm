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
   rope(const char* value);
   rope(std::string_view value);

   template <std::size_t Size> void append(const char (&value)[Size]) {
      append(value, Size - 1U);
   }

   void append(const char* value, std::size_t size);
   void append(const rope& value);
   void append(rope&& value);
   [[nodiscard]] char at(std::size_t index) const;
   [[nodiscard]] char operator[](std::size_t index) const;
   rope& operator+=(const char* value);
   rope& operator+=(const rope& value);
   rope& operator+=(rope&& value);

   friend rope operator+(rope left, const char* right);
   friend rope operator+(rope left, const rope& right);
   friend rope operator+(rope left, rope&& right);

   [[nodiscard]] std::size_t length() const noexcept {
      return _value.size();
   }

   [[nodiscard]] const char* c_str() const noexcept {
      return _value.c_str();
   }

   [[nodiscard]] std::string_view sv() const noexcept {
      return _value;
   }

   void print() const;

 private:
   std::string _value;
};

} // namespace forge::contract
