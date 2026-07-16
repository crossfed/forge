#pragma once

#include <cstdint>

import forge.chain.protocol.values;
import forge.contract.dispatcher;

// Derived from the sequence preprocessor used by Antelope CDT. It exists only
// to preserve the legacy EOSIO_DISPATCH(Type, (action)(action)) spelling.
#define FORGE_EOSIO_DETAIL_STRINGIZE(value) FORGE_EOSIO_DETAIL_STRINGIZE_I(value)
#define FORGE_EOSIO_DETAIL_STRINGIZE_I(value) #value
#define FORGE_EOSIO_DETAIL_CAT(left, right) FORGE_EOSIO_DETAIL_CAT_I(left, right)
#define FORGE_EOSIO_DETAIL_CAT_I(left, right) left##right
#define FORGE_EOSIO_DETAIL_EXPAND(value) FORGE_EOSIO_DETAIL_EXPAND_I(value)
#define FORGE_EOSIO_DETAIL_EXPAND_I(value) value

#define FORGE_EOSIO_DETAIL_GET_NTH_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17,     \
                                       _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, \
                                       name, ...)                                                                      \
   name

#define FORGE_EOSIO_DETAIL_SEQ_SIZE(sequence) FORGE_EOSIO_DETAIL_SEQ_SIZE_I(sequence)
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_I(sequence)                                                                        \
   FORGE_EOSIO_DETAIL_CAT(FORGE_EOSIO_DETAIL_SEQ_SIZE_, FORGE_EOSIO_DETAIL_SEQ_SIZE_0 sequence)
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_0(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_1
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_1(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_2
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_2(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_3
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_3(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_4
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_4(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_5
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_5(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_6
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_6(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_7
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_7(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_8
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_8(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_9
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_9(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_10
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_10(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_11
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_11(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_12
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_12(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_13
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_13(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_14
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_14(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_15
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_15(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_16
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_16(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_17
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_17(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_18
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_18(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_19
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_19(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_20
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_20(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_21
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_21(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_22
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_22(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_23
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_23(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_24
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_24(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_25
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_25(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_26
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_26(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_27
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_27(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_28
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_28(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_29
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_29(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_30
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_30(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_31
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_31(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_32
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_32(_) FORGE_EOSIO_DETAIL_SEQ_SIZE_33

#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_0 0
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_1 1
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_2 2
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_3 3
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_4 4
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_5 5
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_6 6
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_7 7
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_8 8
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_9 9
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_10 10
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_11 11
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_12 12
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_13 13
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_14 14
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_15 15
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_16 16
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_17 17
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_18 18
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_19 19
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_20 20
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_21 21
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_22 22
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_23 23
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_24 24
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_25 25
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_26 26
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_27 27
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_28 28
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_29 29
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_30 30
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_31 31
#define FORGE_EOSIO_DETAIL_SEQ_SIZE_FORGE_EOSIO_DETAIL_SEQ_SIZE_32 32

#define FORGE_EOSIO_DETAIL_SEQ_ENUM(sequence) FORGE_EOSIO_DETAIL_SEQ_ENUM_I(sequence)
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_I(sequence)                                                                        \
   FORGE_EOSIO_DETAIL_CAT(FORGE_EOSIO_DETAIL_SEQ_ENUM_, FORGE_EOSIO_DETAIL_SEQ_SIZE(sequence)) sequence
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_1(value) value
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_2(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_1
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_3(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_2
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_4(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_3
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_5(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_4
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_6(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_5
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_7(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_6
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_8(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_7
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_9(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_8
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_10(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_9
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_11(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_10
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_12(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_11
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_13(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_12
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_14(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_13
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_15(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_14
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_16(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_15
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_17(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_16
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_18(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_17
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_19(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_18
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_20(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_19
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_21(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_20
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_22(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_21
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_23(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_22
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_24(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_23
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_25(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_24
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_26(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_25
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_27(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_26
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_28(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_27
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_29(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_28
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_30(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_29
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_31(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_30
#define FORGE_EOSIO_DETAIL_SEQ_ENUM_32(value) value, FORGE_EOSIO_DETAIL_SEQ_ENUM_31

#define FORGE_EOSIO_DETAIL_FE0(macro, ...)
#define FORGE_EOSIO_DETAIL_FE1(macro, data, value) macro(data, value)
#define FORGE_EOSIO_DETAIL_FE2(macro, data, value, ...)                                                                \
   macro(data, value) FORGE_EOSIO_DETAIL_FE1(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE3(macro, data, value, ...)                                                                \
   macro(data, value) FORGE_EOSIO_DETAIL_FE2(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE4(macro, data, value, ...)                                                                \
   macro(data, value) FORGE_EOSIO_DETAIL_FE3(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE5(macro, data, value, ...)                                                                \
   macro(data, value) FORGE_EOSIO_DETAIL_FE4(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE6(macro, data, value, ...)                                                                \
   macro(data, value) FORGE_EOSIO_DETAIL_FE5(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE7(macro, data, value, ...)                                                                \
   macro(data, value) FORGE_EOSIO_DETAIL_FE6(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE8(macro, data, value, ...)                                                                \
   macro(data, value) FORGE_EOSIO_DETAIL_FE7(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE9(macro, data, value, ...)                                                                \
   macro(data, value) FORGE_EOSIO_DETAIL_FE8(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE10(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE9(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE11(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE10(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE12(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE11(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE13(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE12(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE14(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE13(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE15(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE14(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE16(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE15(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE17(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE16(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE18(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE17(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE19(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE18(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE20(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE19(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE21(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE20(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE22(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE21(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE23(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE22(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE24(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE23(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE25(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE24(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE26(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE25(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE27(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE26(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE28(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE27(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE29(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE28(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE30(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE29(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE31(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE30(macro, data, __VA_ARGS__)
#define FORGE_EOSIO_DETAIL_FE32(macro, data, value, ...)                                                               \
   macro(data, value) FORGE_EOSIO_DETAIL_FE31(macro, data, __VA_ARGS__)

#define FORGE_EOSIO_DETAIL_FOREACH_SEQ_ARGS(macro, data, sequence) (macro, data, FORGE_EOSIO_DETAIL_SEQ_ENUM(sequence))
#define FORGE_EOSIO_DETAIL_GET_NTH_ARG_SEQ_ARGS(...)                                                                   \
   ("ignored", FORGE_EOSIO_DETAIL_SEQ_ENUM(__VA_ARGS__), FORGE_EOSIO_DETAIL_FE32, FORGE_EOSIO_DETAIL_FE31,             \
    FORGE_EOSIO_DETAIL_FE30, FORGE_EOSIO_DETAIL_FE29, FORGE_EOSIO_DETAIL_FE28, FORGE_EOSIO_DETAIL_FE27,                \
    FORGE_EOSIO_DETAIL_FE26, FORGE_EOSIO_DETAIL_FE25, FORGE_EOSIO_DETAIL_FE24, FORGE_EOSIO_DETAIL_FE23,                \
    FORGE_EOSIO_DETAIL_FE22, FORGE_EOSIO_DETAIL_FE21, FORGE_EOSIO_DETAIL_FE20, FORGE_EOSIO_DETAIL_FE19,                \
    FORGE_EOSIO_DETAIL_FE18, FORGE_EOSIO_DETAIL_FE17, FORGE_EOSIO_DETAIL_FE16, FORGE_EOSIO_DETAIL_FE15,                \
    FORGE_EOSIO_DETAIL_FE14, FORGE_EOSIO_DETAIL_FE13, FORGE_EOSIO_DETAIL_FE12, FORGE_EOSIO_DETAIL_FE11,                \
    FORGE_EOSIO_DETAIL_FE10, FORGE_EOSIO_DETAIL_FE9, FORGE_EOSIO_DETAIL_FE8, FORGE_EOSIO_DETAIL_FE7,                   \
    FORGE_EOSIO_DETAIL_FE6, FORGE_EOSIO_DETAIL_FE5, FORGE_EOSIO_DETAIL_FE4, FORGE_EOSIO_DETAIL_FE3,                    \
    FORGE_EOSIO_DETAIL_FE2, FORGE_EOSIO_DETAIL_FE1, FORGE_EOSIO_DETAIL_FE0)
#define FORGE_EOSIO_DETAIL_FOREACH_SEQ(macro, data, ...)                                                               \
   FORGE_EOSIO_DETAIL_EXPAND(                                                                                          \
       FORGE_EOSIO_DETAIL_EXPAND(FORGE_EOSIO_DETAIL_GET_NTH_ARG FORGE_EOSIO_DETAIL_GET_NTH_ARG_SEQ_ARGS(__VA_ARGS__))  \
           FORGE_EOSIO_DETAIL_FOREACH_SEQ_ARGS(macro, data, __VA_ARGS__))

#define FORGE_EOSIO_DETAIL_DISPATCH_ACTION(type, member)                                                               \
   ::forge::contract::make_dispatch_entry<type, &type::member>(                                                        \
       ::forge::chain::protocol::make_name(FORGE_EOSIO_DETAIL_STRINGIZE(member)).value),

#define EOSIO_DISPATCH(type, members)                                                                                  \
   extern "C" [[gnu::visibility("default")]] void apply(std::uint64_t receiver, std::uint64_t code,                    \
                                                        std::uint64_t action) {                                        \
      static constexpr ::forge::contract::dispatch_entry entries[] = {                                                 \
          FORGE_EOSIO_DETAIL_FOREACH_SEQ(FORGE_EOSIO_DETAIL_DISPATCH_ACTION, type, members)};                          \
      ::forge::contract::dispatch(::forge::chain::protocol::name{receiver}, ::forge::chain::protocol::name{code},      \
                                  action, entries);                                                                    \
   }
