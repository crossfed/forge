#pragma once

#include <tuple>

import forge.raw.codec;

#define FORGE_SERIALIZE(type, ...)                                                                                     \
   template <typename Stream> friend void raw_pack(Stream& stream, const type& value) {                                \
      std::apply([&](auto... member) { (::forge::raw::pack(stream, value.*member), ...); }, std::tuple{__VA_ARGS__});  \
   }                                                                                                                   \
   template <typename Stream> friend void raw_unpack(Stream& stream, type& value) {                                    \
      std::apply([&](auto... member) { (::forge::raw::unpack(stream, value.*member), ...); },                          \
                 std::tuple{__VA_ARGS__});                                                                             \
   }
