module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

module forge.contract.print;

namespace forge::contract {

void printhex(const void* data, std::uint32_t size) {
   internal::printhex(data, size);
}

void printl(const char* data, std::size_t size) {
   internal::prints_l(data, static_cast<std::uint32_t>(size));
}

void print(const char* value) {
   internal::prints(value);
}

void print(std::string_view value) {
   printl(value.data(), value.size());
}

void print(const std::string& value) {
   print(std::string_view{value});
}

void print(chain::protocol::name value) {
   internal::printn(value.value);
}

void print(float value) {
   internal::printsf(value);
}

void print(double value) {
   internal::printdf(value);
}

void print(long double value) {
   internal::printqf(&value);
}

void print_f(const char* format) {
   print(format);
}

} // namespace forge::contract
