#pragma once

// `db_get_i64` keeps CDT's historical const-qualified output buffer for source compatibility.
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
                            (const void* value, std::uint32_t size))                                                   \
   FORGE_CONTRACT_INTRINSIC(1, current_receiver, env, current_receiver, std::uint64_t, ())                             \
   FORGE_CONTRACT_INTRINSIC(1, db_store_i64, env, db_store_i64, std::int32_t,                                          \
                            (std::uint64_t scope, std::uint64_t table, std::uint64_t payer, std::uint64_t id,          \
                             const void* data, std::uint32_t len))                                                     \
   FORGE_CONTRACT_INTRINSIC(1, db_update_i64, env, db_update_i64, void,                                                \
                            (std::int32_t iterator, std::uint64_t payer, const void* data, std::uint32_t len))         \
   FORGE_CONTRACT_INTRINSIC(1, db_remove_i64, env, db_remove_i64, void, (std::int32_t iterator))                       \
   FORGE_CONTRACT_INTRINSIC(1, db_get_i64, env, db_get_i64, std::int32_t,                                              \
                            (std::int32_t iterator, const void* data, std::uint32_t len))                              \
   FORGE_CONTRACT_INTRINSIC(1, db_next_i64, env, db_next_i64, std::int32_t,                                            \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_previous_i64, env, db_previous_i64, std::int32_t,                                    \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_find_i64, env, db_find_i64, std::int32_t,                                            \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table, std::uint64_t id))          \
   FORGE_CONTRACT_INTRINSIC(1, db_lowerbound_i64, env, db_lowerbound_i64, std::int32_t,                                \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table, std::uint64_t id))          \
   FORGE_CONTRACT_INTRINSIC(1, db_upperbound_i64, env, db_upperbound_i64, std::int32_t,                                \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table, std::uint64_t id))          \
   FORGE_CONTRACT_INTRINSIC(1, db_end_i64, env, db_end_i64, std::int32_t,                                              \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table))                            \
   FORGE_CONTRACT_INTRINSIC(1, db_idx64_store, env, db_idx64_store, std::int32_t,                                      \
                            (std::uint64_t scope, std::uint64_t table, std::uint64_t payer, std::uint64_t id,          \
                             const std::uint64_t* secondary))                                                          \
   FORGE_CONTRACT_INTRINSIC(1, db_idx64_update, env, db_idx64_update, void,                                            \
                            (std::int32_t iterator, std::uint64_t payer, const std::uint64_t* secondary))              \
   FORGE_CONTRACT_INTRINSIC(1, db_idx64_remove, env, db_idx64_remove, void, (std::int32_t iterator))                   \
   FORGE_CONTRACT_INTRINSIC(1, db_idx64_next, env, db_idx64_next, std::int32_t,                                        \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_idx64_previous, env, db_idx64_previous, std::int32_t,                                \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_idx64_find_primary, env, db_idx64_find_primary, std::int32_t,                        \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table, std::uint64_t* secondary,   \
                             std::uint64_t primary))                                                                   \
   FORGE_CONTRACT_INTRINSIC(1, db_idx64_find_secondary, env, db_idx64_find_secondary, std::int32_t,                    \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table,                             \
                             const std::uint64_t* secondary, std::uint64_t* primary))                                  \
   FORGE_CONTRACT_INTRINSIC(1, db_idx64_lowerbound, env, db_idx64_lowerbound, std::int32_t,                            \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table, std::uint64_t* secondary,   \
                             std::uint64_t* primary))                                                                  \
   FORGE_CONTRACT_INTRINSIC(1, db_idx64_upperbound, env, db_idx64_upperbound, std::int32_t,                            \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table, std::uint64_t* secondary,   \
                             std::uint64_t* primary))                                                                  \
   FORGE_CONTRACT_INTRINSIC(1, db_idx64_end, env, db_idx64_end, std::int32_t,                                          \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table))                            \
   FORGE_CONTRACT_INTRINSIC(1, db_idx128_store, env, db_idx128_store, std::int32_t,                                    \
                            (std::uint64_t scope, std::uint64_t table, std::uint64_t payer, std::uint64_t id,          \
                             const unsigned __int128* secondary))                                                      \
   FORGE_CONTRACT_INTRINSIC(1, db_idx128_update, env, db_idx128_update, void,                                          \
                            (std::int32_t iterator, std::uint64_t payer, const unsigned __int128* secondary))          \
   FORGE_CONTRACT_INTRINSIC(1, db_idx128_remove, env, db_idx128_remove, void, (std::int32_t iterator))                 \
   FORGE_CONTRACT_INTRINSIC(1, db_idx128_next, env, db_idx128_next, std::int32_t,                                      \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_idx128_previous, env, db_idx128_previous, std::int32_t,                              \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_idx128_find_primary, env, db_idx128_find_primary, std::int32_t,                      \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table,                             \
                             unsigned __int128* secondary, std::uint64_t primary))                                     \
   FORGE_CONTRACT_INTRINSIC(1, db_idx128_find_secondary, env, db_idx128_find_secondary, std::int32_t,                  \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table,                             \
                             const unsigned __int128* secondary, std::uint64_t* primary))                              \
   FORGE_CONTRACT_INTRINSIC(1, db_idx128_lowerbound, env, db_idx128_lowerbound, std::int32_t,                          \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table,                             \
                             unsigned __int128* secondary, std::uint64_t* primary))                                    \
   FORGE_CONTRACT_INTRINSIC(1, db_idx128_upperbound, env, db_idx128_upperbound, std::int32_t,                          \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table,                             \
                             unsigned __int128* secondary, std::uint64_t* primary))                                    \
   FORGE_CONTRACT_INTRINSIC(1, db_idx128_end, env, db_idx128_end, std::int32_t,                                        \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table))                            \
   FORGE_CONTRACT_INTRINSIC(1, db_idx256_store, env, db_idx256_store, std::int32_t,                                    \
                            (std::uint64_t scope, std::uint64_t table, std::uint64_t payer, std::uint64_t id,          \
                             const unsigned __int128* data, std::uint32_t data_len))                                   \
   FORGE_CONTRACT_INTRINSIC(                                                                                           \
       1, db_idx256_update, env, db_idx256_update, void,                                                               \
       (std::int32_t iterator, std::uint64_t payer, const unsigned __int128* data, std::uint32_t data_len))            \
   FORGE_CONTRACT_INTRINSIC(1, db_idx256_remove, env, db_idx256_remove, void, (std::int32_t iterator))                 \
   FORGE_CONTRACT_INTRINSIC(1, db_idx256_next, env, db_idx256_next, std::int32_t,                                      \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_idx256_previous, env, db_idx256_previous, std::int32_t,                              \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_idx256_find_primary, env, db_idx256_find_primary, std::int32_t,                      \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table, unsigned __int128* data,    \
                             std::uint32_t data_len, std::uint64_t primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_idx256_find_secondary, env, db_idx256_find_secondary, std::int32_t,                  \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table,                             \
                             const unsigned __int128* data, std::uint32_t data_len, std::uint64_t* primary))           \
   FORGE_CONTRACT_INTRINSIC(1, db_idx256_lowerbound, env, db_idx256_lowerbound, std::int32_t,                          \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table, unsigned __int128* data,    \
                             std::uint32_t data_len, std::uint64_t* primary))                                          \
   FORGE_CONTRACT_INTRINSIC(1, db_idx256_upperbound, env, db_idx256_upperbound, std::int32_t,                          \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table, unsigned __int128* data,    \
                             std::uint32_t data_len, std::uint64_t* primary))                                          \
   FORGE_CONTRACT_INTRINSIC(1, db_idx256_end, env, db_idx256_end, std::int32_t,                                        \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table))                            \
   FORGE_CONTRACT_INTRINSIC(                                                                                           \
       1, db_idx_double_store, env, db_idx_double_store, std::int32_t,                                                 \
       (std::uint64_t scope, std::uint64_t table, std::uint64_t payer, std::uint64_t id, const double* secondary))     \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_double_update, env, db_idx_double_update, void,                                  \
                            (std::int32_t iterator, std::uint64_t payer, const double* secondary))                     \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_double_remove, env, db_idx_double_remove, void, (std::int32_t iterator))         \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_double_next, env, db_idx_double_next, std::int32_t,                              \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_double_previous, env, db_idx_double_previous, std::int32_t,                      \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(                                                                                           \
       1, db_idx_double_find_primary, env, db_idx_double_find_primary, std::int32_t,                                   \
       (std::uint64_t code, std::uint64_t scope, std::uint64_t table, double* secondary, std::uint64_t primary))       \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_double_find_secondary, env, db_idx_double_find_secondary, std::int32_t,          \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table, const double* secondary,    \
                             std::uint64_t* primary))                                                                  \
   FORGE_CONTRACT_INTRINSIC(                                                                                           \
       1, db_idx_double_lowerbound, env, db_idx_double_lowerbound, std::int32_t,                                       \
       (std::uint64_t code, std::uint64_t scope, std::uint64_t table, double* secondary, std::uint64_t* primary))      \
   FORGE_CONTRACT_INTRINSIC(                                                                                           \
       1, db_idx_double_upperbound, env, db_idx_double_upperbound, std::int32_t,                                       \
       (std::uint64_t code, std::uint64_t scope, std::uint64_t table, double* secondary, std::uint64_t* primary))      \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_double_end, env, db_idx_double_end, std::int32_t,                                \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table))                            \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_long_double_store, env, db_idx_long_double_store, std::int32_t,                  \
                            (std::uint64_t scope, std::uint64_t table, std::uint64_t payer, std::uint64_t id,          \
                             const long double* secondary))                                                            \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_long_double_update, env, db_idx_long_double_update, void,                        \
                            (std::int32_t iterator, std::uint64_t payer, const long double* secondary))                \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_long_double_remove, env, db_idx_long_double_remove, void,                        \
                            (std::int32_t iterator))                                                                   \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_long_double_next, env, db_idx_long_double_next, std::int32_t,                    \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_long_double_previous, env, db_idx_long_double_previous, std::int32_t,            \
                            (std::int32_t iterator, std::uint64_t* primary))                                           \
   FORGE_CONTRACT_INTRINSIC(                                                                                           \
       1, db_idx_long_double_find_primary, env, db_idx_long_double_find_primary, std::int32_t,                         \
       (std::uint64_t code, std::uint64_t scope, std::uint64_t table, long double* secondary, std::uint64_t primary))  \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_long_double_find_secondary, env, db_idx_long_double_find_secondary,              \
                            std::int32_t,                                                                              \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table,                             \
                             const long double* secondary, std::uint64_t* primary))                                    \
   FORGE_CONTRACT_INTRINSIC(                                                                                           \
       1, db_idx_long_double_lowerbound, env, db_idx_long_double_lowerbound, std::int32_t,                             \
       (std::uint64_t code, std::uint64_t scope, std::uint64_t table, long double* secondary, std::uint64_t* primary)) \
   FORGE_CONTRACT_INTRINSIC(                                                                                           \
       1, db_idx_long_double_upperbound, env, db_idx_long_double_upperbound, std::int32_t,                             \
       (std::uint64_t code, std::uint64_t scope, std::uint64_t table, long double* secondary, std::uint64_t* primary)) \
   FORGE_CONTRACT_INTRINSIC(1, db_idx_long_double_end, env, db_idx_long_double_end, std::int32_t,                      \
                            (std::uint64_t code, std::uint64_t scope, std::uint64_t table))
