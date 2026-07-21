module;

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

module forge.contract.rope;

import forge.contract.intrinsics;
import forge.contract.print;

namespace forge::contract {

rope::rope(const char* value) : _value(value == nullptr ? "" : value) {}

rope::rope(std::string_view value) : _value(value) {}

void rope::append(const char* value, std::size_t size) {
   check(value != nullptr || size == 0U, "rope source is null");
   _value.append(value, size);
}

void rope::append(const rope& value) {
   _value += value._value;
}

void rope::append(rope&& value) {
   _value += value._value;
}

char rope::at(std::size_t index) const {
   check(index < _value.size(), "rope index is out of range");
   return _value[index];
}

char rope::operator[](std::size_t index) const {
   return at(index);
}

rope& rope::operator+=(const char* value) {
   append(value, std::char_traits<char>::length(value));
   return *this;
}

rope& rope::operator+=(const rope& value) {
   append(value);
   return *this;
}

rope& rope::operator+=(rope&& value) {
   append(std::move(value));
   return *this;
}

rope operator+(rope left, const char* right) {
   return left += right;
}

rope operator+(rope left, const rope& right) {
   return left += right;
}

rope operator+(rope left, rope&& right) {
   return left += std::move(right);
}

void rope::print() const {
   printl(_value.data(), _value.size());
}

} // namespace forge::contract
