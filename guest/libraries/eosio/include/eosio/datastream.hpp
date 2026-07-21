#pragma once

#include <array>
#include <boost/pfr/core.hpp>
#include <concepts>
#include <cstddef>
#include <list>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <type_traits>
#include <utility>

#include <string.h>

extern "C" void* memccpy(void* destination, const void* source, int character, std::size_t size);

import forge.contract.datastream;
import forge.raw.codec;

namespace eosio {

template <typename Storage> class datastream : public forge::contract::datastream<Storage> {
   using base_type = forge::contract::datastream<Storage>;

 public:
   using base_type::base_type;

   datastream(base_type stream) : base_type(std::move(stream)) {}
};

namespace detail {

template <typename T>
inline constexpr bool character_pointer_v =
    std::is_pointer_v<std::remove_cv_t<T>> &&
    std::same_as<std::remove_cv_t<std::remove_pointer_t<std::remove_cv_t<T>>>, char>;

template <typename Stream, typename T>
inline constexpr bool writable_v =
    std::is_arithmetic_v<std::remove_cv_t<T>> || std::is_enum_v<std::remove_cv_t<T>> ||
    std::same_as<std::remove_cv_t<T>, std::byte> || character_pointer_v<T> || std::is_array_v<T> ||
    ::forge::raw::detail::built_in_codec_v<T> || ::forge::raw::detail::adl_packable<Stream, T> ||
    ::forge::raw::detail::traits_packable<Stream, T> || (std::is_class_v<T> && std::is_aggregate_v<T>);

template <typename Stream, typename T>
inline constexpr bool readable_v =
    std::is_arithmetic_v<std::remove_cv_t<T>> || std::is_enum_v<std::remove_cv_t<T>> ||
    std::same_as<std::remove_cv_t<T>, std::byte> || std::is_array_v<T> || ::forge::raw::detail::built_in_codec_v<T> ||
    ::forge::raw::detail::adl_unpackable<Stream, T> || ::forge::raw::detail::traits_unpackable<Stream, T> ||
    (std::is_class_v<T> && std::is_aggregate_v<T>);

} // namespace detail

template <typename Storage, typename T>
   requires(detail::writable_v<datastream<Storage>, T>)
datastream<Storage>& operator<<(datastream<Storage>& stream, const T& value);

template <typename Storage, typename T>
   requires(detail::readable_v<datastream<Storage>, T>)
datastream<Storage>& operator>>(datastream<Storage>& stream, T& value);

template <typename Storage, typename Enable, typename T>
   requires(detail::writable_v<::forge::datastream<Storage, Enable>, T>)::forge::datastream<Storage, Enable>
& operator<<(::forge::datastream<Storage, Enable>& stream, const T& value);

template <typename Storage, typename Enable, typename T>
   requires(detail::readable_v<::forge::datastream<Storage, Enable>, T>)::forge::datastream<Storage, Enable>
& operator>>(::forge::datastream<Storage, Enable>& stream, T& value);

namespace detail {

template <typename Stream, typename T> Stream& write(Stream& stream, const T& value) {
   if constexpr (std::is_array_v<T>) {
      // CDT fixed C arrays are length-prefixed; std::array is not.
      ::forge::raw::pack(stream, ::forge::unsigned_int{std::extent_v<T>});
      for (const auto& item : value) {
         stream << item;
      }
   } else if constexpr (std::is_class_v<T> && std::is_aggregate_v<T> &&
                        !::forge::raw::detail::adl_packable<Stream, T> &&
                        !::forge::raw::detail::traits_packable<Stream, T> &&
                        !::forge::raw::detail::built_in_codec_v<T>) {
      boost::pfr::for_each_field(value, [&](const auto& field) { stream << field; });
   } else if constexpr (::forge::raw::detail::traits_packable<Stream, T> &&
                        !::forge::raw::detail::adl_packable<Stream, T>) {
      ::forge::raw::codec_traits<std::remove_cv_t<T>>::pack(stream, value);
   } else {
      ::forge::raw::pack(stream, value);
   }
   return stream;
}

template <typename Stream, typename T> Stream& read(Stream& stream, T& value) {
   if constexpr (std::is_array_v<T>) {
      // Keep the donor size check before reading the fixed storage.
      auto size = ::forge::unsigned_int{};
      ::forge::raw::unpack(stream, size);
      ::forge::raw::detail::require(size.value == std::extent_v<T>, "T[] size and unpacked size don't match");
      for (auto& item : value) {
         stream >> item;
      }
   } else if constexpr (std::is_class_v<T> && std::is_aggregate_v<T> &&
                        !::forge::raw::detail::adl_unpackable<Stream, T> &&
                        !::forge::raw::detail::traits_unpackable<Stream, T> &&
                        !::forge::raw::detail::built_in_codec_v<T>) {
      boost::pfr::for_each_field(value, [&](auto& field) { stream >> field; });
   } else if constexpr (::forge::raw::detail::traits_unpackable<Stream, T> &&
                        !::forge::raw::detail::adl_unpackable<Stream, T>) {
      ::forge::raw::codec_traits<std::remove_cv_t<T>>::unpack(stream, value);
   } else {
      ::forge::raw::unpack(stream, value);
   }
   return stream;
}

} // namespace detail

template <typename Storage, typename T>
   requires(detail::writable_v<datastream<Storage>, T>)
datastream<Storage>& operator<<(datastream<Storage>& stream, const T& value) {
   return detail::write(stream, value);
}

template <typename Storage, typename T>
   requires(detail::readable_v<datastream<Storage>, T>)
datastream<Storage>& operator>>(datastream<Storage>& stream, T& value) {
   return detail::read(stream, value);
}

template <typename Storage, typename Enable, typename T>
   requires(detail::writable_v<::forge::datastream<Storage, Enable>, T>)::forge::datastream<Storage, Enable>
& operator<<(::forge::datastream<Storage, Enable>& stream, const T& value) {
   return detail::write(stream, value);
}

template <typename Storage, typename Enable, typename T>
   requires(detail::readable_v<::forge::datastream<Storage, Enable>, T>)::forge::datastream<Storage, Enable>
& operator>>(::forge::datastream<Storage, Enable>& stream, T& value) {
   return detail::read(stream, value);
}

} // namespace eosio
