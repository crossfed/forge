#pragma once

#include <eosio/dispatcher.hpp>

import forge.raw.codec;

#define FORGE_EOSIO_DETAIL_PACK_MEMBER(type, member) ::forge::raw::pack(stream, value.member);
#define FORGE_EOSIO_DETAIL_UNPACK_MEMBER(type, member) ::forge::raw::unpack(stream, value.member);

#define EOSLIB_SERIALIZE(type, members)                                                                                \
   template <typename Stream> friend void raw_pack(Stream& stream, const type& value) {                                \
      FORGE_EOSIO_DETAIL_FOREACH_SEQ(FORGE_EOSIO_DETAIL_PACK_MEMBER, type, members)                                    \
   }                                                                                                                   \
   template <typename Stream> friend void raw_unpack(Stream& stream, type& value) {                                    \
      FORGE_EOSIO_DETAIL_FOREACH_SEQ(FORGE_EOSIO_DETAIL_UNPACK_MEMBER, type, members)                                  \
   }
