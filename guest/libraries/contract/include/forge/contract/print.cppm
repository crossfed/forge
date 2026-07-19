module;

#include <forge/contract/intrinsics.h>

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

inline void printhex(const void* data, std::uint32_t size) {
   ::printhex(data, size);
}

inline void printl(const char* data, std::size_t size) {
   ::prints_l(data, static_cast<std::uint32_t>(size));
}

inline void print(const char* value) {
   ::prints(value);
}

inline void print(std::string_view value) {
   printl(value.data(), value.size());
}

inline void print(const std::string& value) {
   print(std::string_view{value});
}

inline void print(chain::protocol::name value) {
   ::printn(value.value);
}

template <std::signed_integral T> inline void print(T value) {
   if constexpr (sizeof(T) == sizeof(__int128)) {
      const auto converted = static_cast<__int128>(value);
      ::printi128(&converted);
   } else if constexpr (sizeof(T) == 1U && !std::same_as<T, bool>) {
      const auto character = static_cast<char>(value);
      ::prints_l(&character, 1U);
   } else {
      ::printi(static_cast<std::int64_t>(value));
   }
}

template <std::unsigned_integral T> inline void print(T value) {
   if constexpr (std::same_as<T, bool>) {
      ::prints(value ? "true" : "false");
   } else if constexpr (sizeof(T) == sizeof(unsigned __int128)) {
      const auto converted = static_cast<unsigned __int128>(value);
      ::printui128(&converted);
   } else {
      ::printui(static_cast<std::uint64_t>(value));
   }
}

inline void print(float value) {
   ::printsf(value);
}

inline void print(double value) {
   ::printdf(value);
}

inline void print(long double value) {
   ::printqf(&value);
}

template <typename T>
concept member_printable = requires(T&& value) { std::forward<T>(value).print(); };

template <member_printable T> inline void print(T&& value) {
   std::forward<T>(value).print();
}

template <typename First, typename... Rest> void print(First&& first, Rest&&... rest) {
   print(std::forward<First>(first));
   (print(std::forward<Rest>(rest)), ...);
}

inline void print_f(const char* format) {
   print(format);
}

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
