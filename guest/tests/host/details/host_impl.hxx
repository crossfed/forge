#pragma once

namespace forge::contract::testing {

class memory_driver;

struct host::impl {
   using wasm = forge::vm::wasm::interpreter;
   using functions = forge::vm::wasm::registered_host_functions<impl>;
   template <typename T, std::size_t Alignment = alignof(T)>
   using input = forge::vm::wasm::argument_proxy<const T*, Alignment>;
   template <typename T, std::size_t Alignment = alignof(T)>
   using output = forge::vm::wasm::argument_proxy<T*, Alignment>;
   template <typename T, std::size_t Alignment = alignof(T)>
   using input_span = forge::vm::wasm::argument_proxy<forge::vm::wasm::span<const T>, Alignment>;
   template <typename T, std::size_t Alignment = alignof(T)>
   using output_span = forge::vm::wasm::argument_proxy<forge::vm::wasm::span<T>, Alignment>;
   using uint128_input = input<unsigned __int128, 16>;
   using uint128_output = output<unsigned __int128, 16>;
   using float128_input = input<float128, 16>;
   using float128_output = output<float128, 16>;

   impl();
   ~impl();

   invocation_result invoke(std::span<const std::uint8_t> code, std::uint64_t receiver, std::uint64_t first_receiver,
                            std::uint64_t action, std::vector<std::uint8_t> data);
   std::optional<table> find_table(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name);
   std::optional<key_value> find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                         std::uint64_t primary);
   template <typename Row, typename Index>
   std::optional<Row> find_secondary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                     std::uint64_t primary);

   void eosio_assert(std::uint32_t test, input<const char, 1> message);
   void eosio_assert_message(std::uint32_t test, forge::vm::wasm::span<const char> message);
   void eosio_assert_code(std::uint32_t test, std::uint64_t code);
   [[noreturn]] void eosio_exit(std::int32_t code);
   std::uint32_t action_data_size() const;
   std::uint32_t read_action_data(forge::vm::wasm::span<char> destination) const;
   void set_action_return_value(forge::vm::wasm::span<const char> value);

   std::int32_t db_store_i64(std::uint64_t scope, std::uint64_t table_name, std::uint64_t payer, std::uint64_t primary,
                             forge::vm::wasm::span<const char> value);
   void db_update_i64(std::int32_t iterator, std::uint64_t payer, forge::vm::wasm::span<const char> value);
   void db_remove_i64(std::int32_t iterator);
   std::int32_t db_get_i64(std::int32_t iterator, output_span<char, 1> value);
   std::int32_t db_next_i64(std::int32_t iterator, output<std::uint64_t> primary);
   std::int32_t db_previous_i64(std::int32_t iterator, output<std::uint64_t> primary);
   std::int32_t db_find_i64(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name, std::uint64_t primary);
   std::int32_t db_lowerbound_i64(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                  std::uint64_t primary);
   std::int32_t db_upperbound_i64(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                  std::uint64_t primary);
   std::int32_t db_end_i64(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name);

#define FORGE_CONTRACT_TEST_DECLARE_SECONDARY(prefix, type, input_type, output_type)                                   \
   std::int32_t prefix##_store(std::uint64_t scope, std::uint64_t table_name, std::uint64_t payer,                     \
                               std::uint64_t primary, input_type secondary);                                           \
   void prefix##_update(std::int32_t iterator, std::uint64_t payer, input_type secondary);                             \
   void prefix##_remove(std::int32_t iterator);                                                                        \
   std::int32_t prefix##_next(std::int32_t iterator, output<std::uint64_t> primary);                                   \
   std::int32_t prefix##_previous(std::int32_t iterator, output<std::uint64_t> primary);                               \
   std::int32_t prefix##_find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,               \
                                      output_type secondary, std::uint64_t primary);                                   \
   std::int32_t prefix##_find_secondary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,             \
                                        input_type secondary, output<std::uint64_t> primary);                          \
   std::int32_t prefix##_lowerbound(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,                 \
                                    output_type secondary, output<std::uint64_t> primary);                             \
   std::int32_t prefix##_upperbound(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,                 \
                                    output_type secondary, output<std::uint64_t> primary);                             \
   std::int32_t prefix##_end(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name)

   FORGE_CONTRACT_TEST_DECLARE_SECONDARY(db_idx64, std::uint64_t, input<std::uint64_t>, output<std::uint64_t>);
   FORGE_CONTRACT_TEST_DECLARE_SECONDARY(db_idx128, unsigned __int128, uint128_input, uint128_output);

   std::int32_t db_idx256_store(std::uint64_t scope, std::uint64_t table_name, std::uint64_t payer,
                                std::uint64_t primary, input_span<unsigned __int128, 16> secondary);
   void db_idx256_update(std::int32_t iterator, std::uint64_t payer, input_span<unsigned __int128, 16> secondary);
   void db_idx256_remove(std::int32_t iterator);
   std::int32_t db_idx256_next(std::int32_t iterator, output<std::uint64_t> primary);
   std::int32_t db_idx256_previous(std::int32_t iterator, output<std::uint64_t> primary);
   std::int32_t db_idx256_find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                       output_span<unsigned __int128, 16> secondary, std::uint64_t primary);
   std::int32_t db_idx256_find_secondary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                         input_span<unsigned __int128, 16> secondary, output<std::uint64_t> primary);
   std::int32_t db_idx256_lowerbound(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                     output_span<unsigned __int128, 16> secondary, output<std::uint64_t> primary);
   std::int32_t db_idx256_upperbound(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                     output_span<unsigned __int128, 16> secondary, output<std::uint64_t> primary);
   std::int32_t db_idx256_end(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name);

   FORGE_CONTRACT_TEST_DECLARE_SECONDARY(db_idx_double, double, input<double>, output<double>);
   FORGE_CONTRACT_TEST_DECLARE_SECONDARY(db_idx_long_double, float128, float128_input, float128_output);

#undef FORGE_CONTRACT_TEST_DECLARE_SECONDARY

   enum class row_kind : std::uint8_t {
      primary,
      index64,
      index128,
      index256,
      index_double,
      index_long_double,
   };

   struct iterator_entry {
      row_kind kind = row_kind::primary;
      forge::ids::object_id id;
      table::id_t table_id;
      bool erased = false;
   };

   template <typename T> T run(boost::asio::awaitable<T> operation);
   void run(boost::asio::awaitable<void> operation);
   void begin_invocation(std::uint64_t receiver, std::vector<std::uint8_t> data);
   void commit_invocation();
   void rollback_invocation();
   void register_intrinsics();
   std::int32_t cache(row_kind kind, forge::ids::object_id id, table::id_t table_id);
   std::int32_t end(row_kind kind, table::id_t table_id);
   iterator_entry& require_iterator(std::int32_t iterator, row_kind kind);
   table::id_t require_end(std::int32_t iterator, row_kind kind) const;
   void erase_iterator(std::int32_t iterator);

   template <typename Row, typename Index, row_kind Kind, typename Secondary>
   std::int32_t secondary_store(std::uint64_t scope, std::uint64_t table_name, std::uint64_t payer,
                                std::uint64_t primary, Secondary secondary);
   template <typename Row, typename Index, row_kind Kind, typename Secondary>
   void secondary_update(std::int32_t iterator, std::uint64_t payer, Secondary secondary);
   template <typename Row, typename Index, row_kind Kind> void secondary_remove(std::int32_t iterator);
   template <typename Row, typename Index, row_kind Kind>
   std::int32_t secondary_next(std::int32_t iterator, std::uint64_t& primary);
   template <typename Row, typename Index, row_kind Kind>
   std::int32_t secondary_previous(std::int32_t iterator, std::uint64_t& primary);
   template <typename Row, typename Index, row_kind Kind, typename Secondary>
   std::int32_t secondary_find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                       Secondary& secondary, std::uint64_t primary);
   template <typename Row, typename Index, row_kind Kind, typename Secondary>
   std::int32_t secondary_find_secondary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                         const Secondary& secondary, std::uint64_t& primary);
   template <typename Row, typename Index, row_kind Kind, typename Secondary>
   std::int32_t secondary_bound(bool upper, std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                Secondary& secondary, std::uint64_t& primary);
   template <typename Row, typename Index, row_kind Kind>
   std::int32_t secondary_end(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name);

   forge::asio::runtime runtime_;
   std::shared_ptr<memory_driver> driver_;
   std::optional<forge::db::object::store> store_;
   std::optional<forge::db::object::transaction> transaction_;
   forge::vm::wasm::wasm_allocator allocator_;
   std::uint64_t receiver_ = 0;
   std::vector<std::uint8_t> action_data_;
   invocation_result result_;
   std::vector<iterator_entry> iterators_;
   std::map<std::tuple<row_kind, std::uint64_t>, std::int32_t> object_iterators_;
   std::map<std::tuple<row_kind, std::uint64_t>, std::int32_t> end_iterators_;
   std::int32_t next_end_ = -2;
};

} // namespace forge::contract::testing
