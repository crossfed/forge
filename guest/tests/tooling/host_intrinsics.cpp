#include <forge/contract/host.hpp>

#include <concepts>
#include <cstdint>
#include <type_traits>

using interface = forge::contract::host::interface;

static_assert(forge::contract::host::interface_version == 1);
static_assert(std::same_as<decltype(&interface::db_store_i64),
                           std::int32_t (interface::*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                                                       const void*, std::uint32_t)>);
static_assert(std::same_as<decltype(&interface::db_get_i64),
                           std::int32_t (interface::*)(std::int32_t, const void*, std::uint32_t)>);
static_assert(std::same_as<decltype(&interface::db_idx128_find_primary),
                           std::int32_t (interface::*)(std::uint64_t, std::uint64_t, std::uint64_t, unsigned __int128*,
                                                       std::uint64_t)>);
static_assert(std::same_as<decltype(&interface::db_idx256_find_secondary),
                           std::int32_t (interface::*)(std::uint64_t, std::uint64_t, std::uint64_t,
                                                       const unsigned __int128*, std::uint32_t, std::uint64_t*)>);
static_assert(
    std::same_as<decltype(&interface::db_idx_double_lowerbound),
                 std::int32_t (interface::*)(std::uint64_t, std::uint64_t, std::uint64_t, double*, std::uint64_t*)>);
static_assert(std::same_as<decltype(&interface::db_idx_long_double_upperbound),
                           std::int32_t (interface::*)(std::uint64_t, std::uint64_t, std::uint64_t, long double*,
                                                       std::uint64_t*)>);
