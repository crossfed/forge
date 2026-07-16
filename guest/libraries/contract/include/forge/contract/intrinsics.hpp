#pragma once

#define FORGE_CONTRACT_INTRINSICS(FORGE_CONTRACT_INTRINSIC)                                                            \
   FORGE_CONTRACT_INTRINSIC(1, eosio_assert, env, eosio_assert, void, (std::uint32_t test, const char* message))       \
   FORGE_CONTRACT_INTRINSIC(1, eosio_assert_message, env, eosio_assert_message, void,                                  \
                            (std::uint32_t test, const char* message, std::uint32_t size))                             \
   FORGE_CONTRACT_INTRINSIC(1, eosio_assert_code, env, eosio_assert_code, void,                                        \
                            (std::uint32_t test, std::uint64_t code))                                                  \
   FORGE_CONTRACT_INTRINSIC(1, eosio_exit, env, eosio_exit, void, (std::int32_t code))                                 \
   FORGE_CONTRACT_INTRINSIC(1, action_data_size, env, action_data_size, std::uint32_t, ())                             \
   FORGE_CONTRACT_INTRINSIC(1, read_action_data, env, read_action_data, std::uint32_t,                                 \
                            (void* destination, std::uint32_t size))                                                   \
   FORGE_CONTRACT_INTRINSIC(1, set_action_return_value, env, set_action_return_value, void,                            \
                            (const void* value, std::uint32_t size))
