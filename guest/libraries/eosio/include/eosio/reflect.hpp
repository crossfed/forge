#pragma once

#define FORGE_EOSIO_REFLECT_CAT_I(left, right) left##right
#define FORGE_EOSIO_REFLECT_CAT(left, right) FORGE_EOSIO_REFLECT_CAT_I(left, right)
#define FORGE_EOSIO_REFLECT_COUNT_I(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, count, \
                                    ...)                                                                              \
   count
#define FORGE_EOSIO_REFLECT_COUNT(...)                                                                                \
   FORGE_EOSIO_REFLECT_COUNT_I(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define FORGE_EOSIO_REFLECT_MEMBER(member) visitor(member);
#define FORGE_EOSIO_REFLECT_1(a) FORGE_EOSIO_REFLECT_MEMBER(a)
#define FORGE_EOSIO_REFLECT_2(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_1(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_3(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_2(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_4(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_3(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_5(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_4(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_6(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_5(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_7(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_6(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_8(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_7(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_9(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_8(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_10(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_9(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_11(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_10(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_12(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_11(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_13(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_12(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_14(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_13(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_15(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_14(__VA_ARGS__)
#define FORGE_EOSIO_REFLECT_16(a, ...) FORGE_EOSIO_REFLECT_MEMBER(a) FORGE_EOSIO_REFLECT_15(__VA_ARGS__)
#define CDT_REFLECT(...)                                                                                              \
   template <typename Visitor> constexpr void forge_contract_for_each_field(Visitor&& visitor) const {               \
      FORGE_EOSIO_REFLECT_CAT(FORGE_EOSIO_REFLECT_, FORGE_EOSIO_REFLECT_COUNT(__VA_ARGS__))(__VA_ARGS__)             \
   }
