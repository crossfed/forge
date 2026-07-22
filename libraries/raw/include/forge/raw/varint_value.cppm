module;

#include <cstdint>
#include <functional>
#include <type_traits>

export module forge.raw.varint_value;

export namespace forge {

struct unsigned_int {
   using base_uint = std::uint32_t;

   constexpr unsigned_int(base_uint value = 0) : value(value) {}

   template <typename T>
      requires(std::is_integral_v<T> || std::is_enum_v<T>)
   constexpr unsigned_int(T value) : value(static_cast<base_uint>(value)) {}

   constexpr operator std::uint32_t() const {
      return value;
   }

   constexpr unsigned_int& operator=(std::int32_t other) {
      value = static_cast<base_uint>(other);
      return *this;
   }

   constexpr auto operator<=>(const unsigned_int&) const = default;

   base_uint value;
};

struct signed_int {
   using base_int = std::int32_t;

   constexpr signed_int(base_int value = 0) : value(value) {}

   constexpr operator std::int32_t() const {
      return value;
   }

   template <typename T> constexpr signed_int& operator=(const T& other) {
      value = static_cast<base_int>(other);
      return *this;
   }

   constexpr signed_int operator++(int) {
      const auto previous = *this;
      ++value;
      return previous;
   }

   constexpr signed_int& operator++() {
      ++value;
      return *this;
   }

   constexpr auto operator<=>(const signed_int&) const = default;

   base_int value;
};

} // namespace forge

export namespace std {

template <> struct hash<forge::signed_int> {
   size_t operator()(const forge::signed_int& value) const {
      return std::hash<forge::signed_int::base_int>{}(value.value);
   }
};

template <> struct hash<forge::unsigned_int> {
   size_t operator()(const forge::unsigned_int& value) const {
      return std::hash<forge::unsigned_int::base_uint>{}(value.value);
   }
};

} // namespace std
