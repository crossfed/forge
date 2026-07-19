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

#include <string.h>

extern "C" void* memccpy(void* destination, const void* source, int character, std::size_t size);

import forge.contract.datastream;
import forge.raw.codec;

namespace eosio {

using forge::contract::datastream;

template <typename Storage, typename T> datastream<Storage>& operator<<(datastream<Storage>& stream, const T& value) {
   if constexpr (std::is_class_v<T> && std::is_aggregate_v<T> &&
                 !::forge::raw::detail::adl_packable<datastream<Storage>, T> &&
                 !::forge::raw::detail::built_in_codec_v<T>) {
      boost::pfr::for_each_field(value, [&](const auto& field) { stream << field; });
   } else {
      ::forge::raw::pack(stream, value);
   }
   return stream;
}

template <typename Storage, typename T> datastream<Storage>& operator>>(datastream<Storage>& stream, T& value) {
   if constexpr (std::is_class_v<T> && std::is_aggregate_v<T> &&
                 !::forge::raw::detail::adl_unpackable<datastream<Storage>, T> &&
                 !::forge::raw::detail::built_in_codec_v<T>) {
      boost::pfr::for_each_field(value, [&](auto& field) { stream >> field; });
   } else {
      ::forge::raw::unpack(stream, value);
   }
   return stream;
}

} // namespace eosio
