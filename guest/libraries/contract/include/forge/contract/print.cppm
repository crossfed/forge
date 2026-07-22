module;

#include <forge/contract/internal/intrinsics.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

export module forge.contract.print;

export import forge.chain.protocol.values;

export namespace forge::contract {

void printhex(const void* data, std::uint32_t size);
void printl(const char* data, std::size_t size);
void print(const char* value);
void print(std::string_view value);
void print(const std::string& value);
void print(chain::protocol::name value);

template <std::signed_integral T> inline void print(T value) {
   if constexpr (sizeof(T) == sizeof(__int128)) {
      const auto converted = static_cast<__int128>(value);
      ::forge::contract::internal::printi128(&converted);
   } else if constexpr (sizeof(T) == 1U && !std::same_as<T, bool>) {
      const auto character = static_cast<char>(value);
      ::forge::contract::internal::prints_l(&character, 1U);
   } else {
      ::forge::contract::internal::printi(static_cast<std::int64_t>(value));
   }
}

template <std::unsigned_integral T> inline void print(T value) {
   if constexpr (std::same_as<T, bool>) {
      ::forge::contract::internal::prints(value ? "true" : "false");
   } else if constexpr (sizeof(T) == sizeof(unsigned __int128)) {
      const auto converted = static_cast<unsigned __int128>(value);
      ::forge::contract::internal::printui128(&converted);
   } else {
      ::forge::contract::internal::printui(static_cast<std::uint64_t>(value));
   }
}

void print(float value);
void print(double value);
void print(long double value);

template <typename T>
concept member_printable = requires(T&& value) { std::forward<T>(value).print(); };

template <member_printable T> inline void print(T&& value) {
   std::forward<T>(value).print();
}

template <typename First, typename... Rest> void print(First&& first, Rest&&... rest) {
   print(std::forward<First>(first));
   (print(std::forward<Rest>(rest)), ...);
}

void print_f(const char* format);

template <typename Arg, typename... Args> void print_f(const char* format, Arg&& value, Args&&... rest) {
   while (*format != '\0') {
      if (*format == '%') {
         print(std::forward<Arg>(value));
         print_f(format + 1, std::forward<Args>(rest)...);
         return;
      }
      printl(format, 1U);
      ++format;
   }
}

class iostream {};
inline iostream cout;

template <typename T> iostream& operator<<(iostream& stream, T&& value) {
   print(std::forward<T>(value));
   return stream;
}

} // namespace forge::contract
