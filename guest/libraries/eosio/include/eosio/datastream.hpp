#pragma once

#include <array>
#include <boost/pfr/core.hpp>
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

template <typename Storage, typename T> datastream<Storage>& operator<<(datastream<Storage>& stream, const T& value);
template <typename Storage, typename T> datastream<Storage>& operator>>(datastream<Storage>& stream, T& value);

template <typename Storage, typename Enable, typename T>
::forge::datastream<Storage, Enable>& operator<<(::forge::datastream<Storage, Enable>& stream, const T& value);

template <typename Storage, typename Enable, typename T>
::forge::datastream<Storage, Enable>& operator>>(::forge::datastream<Storage, Enable>& stream, T& value);

namespace detail {

template <typename Stream, typename T> Stream& write(Stream& stream, const T& value) {
   if constexpr (std::is_class_v<T> && std::is_aggregate_v<T> && !::forge::raw::detail::adl_packable<Stream, T> &&
                 !::forge::raw::detail::built_in_codec_v<T>) {
      boost::pfr::for_each_field(value, [&](const auto& field) { stream << field; });
   } else {
      ::forge::raw::pack(stream, value);
   }
   return stream;
}

template <typename Stream, typename T> Stream& read(Stream& stream, T& value) {
   if constexpr (std::is_class_v<T> && std::is_aggregate_v<T> && !::forge::raw::detail::adl_unpackable<Stream, T> &&
                 !::forge::raw::detail::built_in_codec_v<T>) {
      boost::pfr::for_each_field(value, [&](auto& field) { stream >> field; });
   } else {
      ::forge::raw::unpack(stream, value);
   }
   return stream;
}

} // namespace detail

template <typename Storage, typename T> datastream<Storage>& operator<<(datastream<Storage>& stream, const T& value) {
   return detail::write(stream, value);
}

template <typename Storage, typename T> datastream<Storage>& operator>>(datastream<Storage>& stream, T& value) {
   return detail::read(stream, value);
}

template <typename Storage, typename Enable, typename T>
::forge::datastream<Storage, Enable>& operator<<(::forge::datastream<Storage, Enable>& stream, const T& value) {
   return detail::write(stream, value);
}

template <typename Storage, typename Enable, typename T>
::forge::datastream<Storage, Enable>& operator>>(::forge::datastream<Storage, Enable>& stream, T& value) {
   return detail::read(stream, value);
}

} // namespace eosio
