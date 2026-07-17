module;

#include <boost/asio/awaitable.hpp>
#include <forge/contract/intrinsics.hpp>
#include <forge/exceptions/macros.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

module forge.contract.testing.host;

import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.core.driver;
import forge.db.object.index;
import forge.db.object.store;
import forge.db.object.transaction;
import forge.ids.object_id;
import forge.vm.wasm.backend;

#include "details/memory_driver.hxx"
#include "details/host_impl.hxx"
#include "details/softfloat.hxx"

namespace forge::contract::testing {

namespace {

constexpr auto max_assert_message_size = std::size_t{1024};

class exit_signal final {
 public:
   explicit exit_signal(std::int32_t value) : code{value} {}

   std::int32_t code = 0;
};

[[noreturn]] void fail_database(std::string_view message) {
   FORGE_THROW_EXCEPTION(exceptions::database_error, std::string{message});
}

[[noreturn]] void fail_iterator(std::string_view message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_iterator, std::string{message});
}

void require_payer(std::uint64_t payer) {
   if (payer == 0U) {
      fail_database("database payer must not be zero");
   }
}

void require_owner(const table& value, std::uint64_t receiver) {
   if (value.code != receiver) {
      fail_database("database row belongs to a different contract");
   }
}

void require_ordered(float64 value) {
   if (is_nan(value)) {
      fail_database("NaN is not a valid secondary key");
   }
}

void require_ordered(float128 value) {
   if (is_nan(value)) {
      fail_database("NaN is not a valid secondary key");
   }
}

void require_ordered(std::uint64_t) {}
void require_ordered(unsigned __int128) {}
void require_ordered(const uint256&) {}

boost::asio::awaitable<std::optional<table>> lookup_table(forge::db::object::transaction& transaction,
                                                          std::uint64_t code, std::uint64_t scope,
                                                          std::uint64_t table_name) {
   co_return co_await transaction.index<table_index, by_code_scope_table>().find(code, scope, table_name);
}

boost::asio::awaitable<table> ensure_table(forge::db::object::transaction& transaction, std::uint64_t code,
                                           std::uint64_t scope, std::uint64_t table_name, std::uint64_t payer) {
   if (auto found = co_await lookup_table(transaction, code, scope, table_name)) {
      co_return *found;
   }
   require_payer(payer);
   co_return co_await transaction.create<table>([&](table& value) {
      value.code = code;
      value.scope = scope;
      value.table_name = table_name;
      value.payer = payer;
   });
}

boost::asio::awaitable<void> increment_table(forge::db::object::transaction& transaction, table::id_t id) {
   co_await transaction.modify(id, [](table& value) {
      if (value.count == std::numeric_limits<std::uint32_t>::max()) {
         fail_database("database table row count overflow");
      }
      ++value.count;
   });
}

boost::asio::awaitable<void> decrement_table(forge::db::object::transaction& transaction, table::id_t id) {
   const auto value = co_await transaction.get(id);
   if (value.count == 0U) {
      fail_database("database table row count underflow");
   }
   if (value.count == 1U) {
      co_await transaction.erase(id);
   } else {
      co_await transaction.modify(id, [](table& current) { --current.count; });
   }
}

template <typename Row> [[nodiscard]] bool same_table(const std::optional<Row>& row, table::id_t id) {
   return row && row->table_id == id;
}

} // namespace

template <typename T> T host::impl::run(boost::asio::awaitable<T> operation) {
   return forge::asio::blocking::run(runtime_, std::move(operation));
}

void host::impl::run(boost::asio::awaitable<void> operation) {
   forge::asio::blocking::run(runtime_, std::move(operation));
}

host::impl::impl()
    : runtime_{forge::asio::runtime_options{.worker_threads = 1, .thread_name = "contract-db-test"}},
      driver_{std::make_shared<memory_driver>()} {
   store_.emplace(run(forge::db::object::store::open(driver_)));
   store_->register_object<table_index>();
   store_->register_object<key_value_index>();
   store_->register_object<index64_index>();
   store_->register_object<index128_index>();
   store_->register_object<index256_index>();
   store_->register_object<index_double_index>();
   store_->register_object<index_long_double_index>();
   register_intrinsics();
}

host::impl::~impl() {
   if (transaction_) {
      try {
         rollback_invocation();
      } catch (...) {
      }
   }
   allocator_.free();
}

void host::impl::register_intrinsics() {
   static const auto registered = [] {
#define FORGE_CONTRACT_TEST_REGISTER(version, identifier, module_name, import_name, result, arguments)                 \
   functions::add<&impl::identifier>(#module_name, #import_name);
      FORGE_CONTRACT_INTRINSICS(FORGE_CONTRACT_TEST_REGISTER)
#undef FORGE_CONTRACT_TEST_REGISTER
      return true;
   }();
   static_cast<void>(registered);
}

void host::impl::begin_invocation(std::uint64_t receiver, std::vector<std::uint8_t> data) {
   if (transaction_) {
      fail_database("contract database invocation is already active");
   }
   transaction_.emplace(run(store_->begin_transaction()));
   receiver_ = receiver;
   action_data_ = std::move(data);
   result_ = {};
   iterators_.clear();
   object_iterators_.clear();
   end_iterators_.clear();
   next_end_ = -2;
}

void host::impl::commit_invocation() {
   run(transaction_->commit());
   transaction_.reset();
}

void host::impl::rollback_invocation() {
   run(transaction_->rollback());
   transaction_.reset();
}

invocation_result host::impl::invoke(std::span<const std::uint8_t> code, std::uint64_t receiver,
                                     std::uint64_t first_receiver, std::uint64_t action,
                                     std::vector<std::uint8_t> data) {
   begin_invocation(receiver, std::move(data));
   auto bytes = forge::vm::wasm::wasm_code{code.begin(), code.end()};
   try {
      auto vm =
          forge::vm::wasm::backend<functions, wasm, forge::vm::wasm::compatibility_options>{bytes, *this, &allocator_};
      vm(*this, "env", "apply", receiver, first_receiver, action);
      commit_invocation();
   } catch (const exit_signal& signal) {
      result_.exit_code = signal.code;
      commit_invocation();
   } catch (...) {
      const auto failure = std::current_exception();
      rollback_invocation();
      std::rethrow_exception(failure);
   }
   return result_;
}

std::optional<table> host::impl::find_table(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name) {
   return run(store_->index<table_index, by_code_scope_table>().find(code, scope, table_name));
}

std::optional<key_value> host::impl::find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                                  std::uint64_t primary) {
   const auto owner = find_table(code, scope, table_name);
   if (!owner) {
      return std::nullopt;
   }
   return run(store_->index<key_value_index, by_scope_primary>().find(owner->id, primary));
}

template <typename Row, typename Index>
std::optional<Row> host::impl::find_secondary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                              std::uint64_t primary) {
   const auto owner = find_table(code, scope, table_name);
   if (!owner) {
      return std::nullopt;
   }
   return run(store_->index<Index, by_primary>().find(owner->id, primary));
}

template std::optional<index64> host::impl::find_secondary<index64, index64_index>(std::uint64_t, std::uint64_t,
                                                                                   std::uint64_t, std::uint64_t);
template std::optional<index128> host::impl::find_secondary<index128, index128_index>(std::uint64_t, std::uint64_t,
                                                                                      std::uint64_t, std::uint64_t);
template std::optional<index256> host::impl::find_secondary<index256, index256_index>(std::uint64_t, std::uint64_t,
                                                                                      std::uint64_t, std::uint64_t);
template std::optional<index_double> host::impl::find_secondary<index_double, index_double_index>(std::uint64_t,
                                                                                                  std::uint64_t,
                                                                                                  std::uint64_t,
                                                                                                  std::uint64_t);
template std::optional<index_long_double>
host::impl::find_secondary<index_long_double, index_long_double_index>(std::uint64_t, std::uint64_t, std::uint64_t,
                                                                       std::uint64_t);

std::int32_t host::impl::cache(row_kind kind, forge::ids::object_id id, table::id_t table_id) {
   const auto key = std::tuple{kind, id.instance};
   if (const auto found = object_iterators_.find(key); found != object_iterators_.end()) {
      return found->second;
   }
   if (iterators_.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
      fail_iterator("contract database iterator cache overflow");
   }
   const auto result = static_cast<std::int32_t>(iterators_.size());
   iterators_.push_back(iterator_entry{.kind = kind, .id = id, .table_id = table_id});
   object_iterators_.emplace(key, result);
   return result;
}

std::int32_t host::impl::end(row_kind kind, table::id_t table_id) {
   const auto key = std::tuple{kind, table_id.instance};
   if (const auto found = end_iterators_.find(key); found != end_iterators_.end()) {
      return found->second;
   }
   if (next_end_ == std::numeric_limits<std::int32_t>::min()) {
      fail_iterator("contract database end iterator cache overflow");
   }
   const auto result = next_end_--;
   end_iterators_.emplace(key, result);
   return result;
}

host::impl::iterator_entry& host::impl::require_iterator(std::int32_t iterator, row_kind kind) {
   if (iterator < 0 || static_cast<std::size_t>(iterator) >= iterators_.size()) {
      fail_iterator("contract database iterator is invalid");
   }
   auto& entry = iterators_[static_cast<std::size_t>(iterator)];
   if (entry.erased || entry.kind != kind) {
      fail_iterator("contract database iterator has the wrong row type");
   }
   return entry;
}

table::id_t host::impl::require_end(std::int32_t iterator, row_kind kind) const {
   for (const auto& [key, value] : end_iterators_) {
      if (value == iterator && std::get<0>(key) == kind) {
         return table::id_t{std::get<1>(key)};
      }
   }
   fail_iterator("contract database end iterator is invalid");
}

void host::impl::erase_iterator(std::int32_t iterator) {
   auto& entry = iterators_[static_cast<std::size_t>(iterator)];
   object_iterators_.erase(std::tuple{entry.kind, entry.id.instance});
   entry.erased = true;
}

void host::impl::eosio_assert(std::uint32_t test, input<const char, 1> message) {
   if (test == 0U) {
      const auto size = strnlen(message.get(), max_assert_message_size);
      const auto text = std::string{message.get(), size};
      FORGE_THROW_EXCEPTION(exceptions::assertion_failure, text);
   }
}

void host::impl::eosio_assert_message(std::uint32_t test, forge::vm::wasm::span<const char> message) {
   if (test == 0U) {
      const auto size = std::min(message.size(), max_assert_message_size);
      const auto text = std::string{message.data(), size};
      FORGE_THROW_EXCEPTION(exceptions::assertion_failure, text);
   }
}

void host::impl::eosio_assert_code(std::uint32_t test, std::uint64_t code) {
   if (test == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::assertion_failure, "contract assertion code",
                            forge::exceptions::ctx("assertion_code", code));
   }
}

[[noreturn]] void host::impl::eosio_exit(std::int32_t code) {
   throw exit_signal{code};
}

std::uint32_t host::impl::action_data_size() const {
   return static_cast<std::uint32_t>(action_data_.size());
}

std::uint32_t host::impl::read_action_data(forge::vm::wasm::span<char> destination) const {
   if (destination.empty()) {
      return static_cast<std::uint32_t>(action_data_.size());
   }
   const auto size = std::min(destination.size(), action_data_.size());
   std::copy_n(action_data_.begin(), size, destination.begin());
   return static_cast<std::uint32_t>(size);
}

void host::impl::set_action_return_value(forge::vm::wasm::span<const char> value) {
   result_.return_value.assign(reinterpret_cast<const std::uint8_t*>(value.data()),
                               reinterpret_cast<const std::uint8_t*>(value.data() + value.size()));
}

std::int32_t host::impl::db_store_i64(std::uint64_t scope, std::uint64_t table_name, std::uint64_t payer,
                                      std::uint64_t primary, forge::vm::wasm::span<const char> value) {
   require_payer(payer);
   const auto owner = run(ensure_table(*transaction_, receiver_, scope, table_name, payer));
   if (run(transaction_->index<key_value_index, by_scope_primary>().find(owner.id, primary))) {
      fail_database("database primary key already exists");
   }
   const auto created = run(transaction_->create<key_value>([&](key_value& row) {
      row.table_id = owner.id;
      row.primary = primary;
      row.payer = payer;
      row.value.assign(reinterpret_cast<const std::uint8_t*>(value.data()),
                       reinterpret_cast<const std::uint8_t*>(value.data() + value.size()));
   }));
   run(increment_table(*transaction_, owner.id));
   return cache(row_kind::primary, created.id.as_object_id(), owner.id);
}

void host::impl::db_update_i64(std::int32_t iterator, std::uint64_t payer, forge::vm::wasm::span<const char> value) {
   auto& entry = require_iterator(iterator, row_kind::primary);
   const auto owner = run(transaction_->get(entry.table_id));
   require_owner(owner, receiver_);
   run(transaction_->modify(key_value::id_t{entry.id.instance}, [&](key_value& row) {
      if (payer != 0U) {
         row.payer = payer;
      }
      row.value.assign(reinterpret_cast<const std::uint8_t*>(value.data()),
                       reinterpret_cast<const std::uint8_t*>(value.data() + value.size()));
   }));
}

void host::impl::db_remove_i64(std::int32_t iterator) {
   auto& entry = require_iterator(iterator, row_kind::primary);
   const auto owner = run(transaction_->get(entry.table_id));
   require_owner(owner, receiver_);
   run(transaction_->erase(key_value::id_t{entry.id.instance}));
   run(decrement_table(*transaction_, entry.table_id));
   erase_iterator(iterator);
}

std::int32_t host::impl::db_get_i64(std::int32_t iterator, output_span<char, 1> value) {
   const auto& entry = require_iterator(iterator, row_kind::primary);
   const auto row = run(transaction_->get(key_value::id_t{entry.id.instance}));
   if (value.empty()) {
      return static_cast<std::int32_t>(row.value.size());
   }
   const auto size = std::min(value.size(), row.value.size());
   std::copy_n(row.value.begin(), size, value.begin());
   return static_cast<std::int32_t>(size);
}

std::int32_t host::impl::db_next_i64(std::int32_t iterator, output<std::uint64_t> primary) {
   const auto& entry = require_iterator(iterator, row_kind::primary);
   const auto current = run(transaction_->get(key_value::id_t{entry.id.instance}));
   auto index = transaction_->index<key_value_index, by_scope_primary>();
   const auto rank = run(index.rank(current));
   const auto next = run(index.nth(rank + 1U));
   if (!same_table(next, entry.table_id)) {
      return end(row_kind::primary, entry.table_id);
   }
   *primary = next->primary;
   return cache(row_kind::primary, next->id.as_object_id(), next->table_id);
}

std::int32_t host::impl::db_previous_i64(std::int32_t iterator, output<std::uint64_t> primary) {
   auto index = transaction_->index<key_value_index, by_scope_primary>();
   auto table_id = table::id_t{};
   auto rank = std::uint64_t{};
   if (iterator < -1) {
      table_id = require_end(iterator, row_kind::primary);
      rank = run(index.equal_range_rank(table_id)).second;
   } else {
      const auto& entry = require_iterator(iterator, row_kind::primary);
      table_id = entry.table_id;
      const auto current = run(transaction_->get(key_value::id_t{entry.id.instance}));
      rank = run(index.rank(current));
   }
   if (rank == 0U) {
      return -1;
   }
   const auto previous = run(index.nth(rank - 1U));
   if (!same_table(previous, table_id)) {
      return -1;
   }
   *primary = previous->primary;
   return cache(row_kind::primary, previous->id.as_object_id(), previous->table_id);
}

std::int32_t host::impl::db_find_i64(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                     std::uint64_t primary) {
   const auto owner = run(lookup_table(*transaction_, code, scope, table_name));
   if (!owner) {
      return -1;
   }
   const auto row = run(transaction_->index<key_value_index, by_scope_primary>().find(owner->id, primary));
   if (!row) {
      return end(row_kind::primary, owner->id);
   }
   return cache(row_kind::primary, row->id.as_object_id(), row->table_id);
}

std::int32_t host::impl::db_lowerbound_i64(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                           std::uint64_t primary) {
   const auto owner = run(lookup_table(*transaction_, code, scope, table_name));
   if (!owner) {
      return -1;
   }
   auto index = transaction_->index<key_value_index, by_scope_primary>();
   const auto row = run(index.nth(run(index.lower_bound_rank(owner->id, primary))));
   if (!same_table(row, owner->id)) {
      return end(row_kind::primary, owner->id);
   }
   return cache(row_kind::primary, row->id.as_object_id(), row->table_id);
}

std::int32_t host::impl::db_upperbound_i64(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                           std::uint64_t primary) {
   const auto owner = run(lookup_table(*transaction_, code, scope, table_name));
   if (!owner) {
      return -1;
   }
   auto index = transaction_->index<key_value_index, by_scope_primary>();
   const auto row = run(index.nth(run(index.upper_bound_rank(owner->id, primary))));
   if (!same_table(row, owner->id)) {
      return end(row_kind::primary, owner->id);
   }
   return cache(row_kind::primary, row->id.as_object_id(), row->table_id);
}

std::int32_t host::impl::db_end_i64(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name) {
   const auto owner = run(lookup_table(*transaction_, code, scope, table_name));
   return owner ? end(row_kind::primary, owner->id) : -1;
}

template <typename Row, typename Index, host::impl::row_kind Kind, typename Secondary>
std::int32_t host::impl::secondary_store(std::uint64_t scope, std::uint64_t table_name, std::uint64_t payer,
                                         std::uint64_t primary, Secondary secondary) {
   require_payer(payer);
   require_ordered(secondary);
   const auto owner = run(ensure_table(*transaction_, receiver_, scope, table_name, payer));
   if (run(transaction_->index<Index, by_primary>().find(owner.id, primary))) {
      fail_database("database secondary primary key already exists");
   }
   const auto created = run(transaction_->create<Row>([&](Row& row) {
      row.table_id = owner.id;
      row.primary = primary;
      row.payer = payer;
      row.secondary = secondary;
   }));
   run(increment_table(*transaction_, owner.id));
   return cache(Kind, created.id.as_object_id(), owner.id);
}

template <typename Row, typename Index, host::impl::row_kind Kind, typename Secondary>
void host::impl::secondary_update(std::int32_t iterator, std::uint64_t payer, Secondary secondary) {
   require_ordered(secondary);
   const auto& entry = require_iterator(iterator, Kind);
   const auto owner = run(transaction_->get(entry.table_id));
   require_owner(owner, receiver_);
   run(transaction_->modify(typename Row::id_t{entry.id.instance}, [&](Row& row) {
      if (payer != 0U) {
         row.payer = payer;
      }
      row.secondary = secondary;
   }));
}

template <typename Row, typename Index, host::impl::row_kind Kind>
void host::impl::secondary_remove(std::int32_t iterator) {
   const auto& entry = require_iterator(iterator, Kind);
   const auto owner = run(transaction_->get(entry.table_id));
   require_owner(owner, receiver_);
   run(transaction_->erase(typename Row::id_t{entry.id.instance}));
   run(decrement_table(*transaction_, entry.table_id));
   erase_iterator(iterator);
}

template <typename Row, typename Index, host::impl::row_kind Kind>
std::int32_t host::impl::secondary_next(std::int32_t iterator, std::uint64_t& primary) {
   const auto& entry = require_iterator(iterator, Kind);
   const auto current = run(transaction_->get(typename Row::id_t{entry.id.instance}));
   auto index = transaction_->index<Index, by_secondary>();
   const auto next = run(index.nth(run(index.rank(current)) + 1U));
   if (!same_table(next, entry.table_id)) {
      return end(Kind, entry.table_id);
   }
   primary = next->primary;
   return cache(Kind, next->id.as_object_id(), next->table_id);
}

template <typename Row, typename Index, host::impl::row_kind Kind>
std::int32_t host::impl::secondary_previous(std::int32_t iterator, std::uint64_t& primary) {
   auto index = transaction_->index<Index, by_secondary>();
   auto table_id = table::id_t{};
   auto rank = std::uint64_t{};
   if (iterator < -1) {
      table_id = require_end(iterator, Kind);
      rank = run(index.equal_range_rank(table_id)).second;
   } else {
      const auto& entry = require_iterator(iterator, Kind);
      table_id = entry.table_id;
      const auto current = run(transaction_->get(typename Row::id_t{entry.id.instance}));
      rank = run(index.rank(current));
   }
   if (rank == 0U) {
      return -1;
   }
   const auto previous = run(index.nth(rank - 1U));
   if (!same_table(previous, table_id)) {
      return -1;
   }
   primary = previous->primary;
   return cache(Kind, previous->id.as_object_id(), previous->table_id);
}

template <typename Row, typename Index, host::impl::row_kind Kind, typename Secondary>
std::int32_t host::impl::secondary_find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                                Secondary& secondary, std::uint64_t primary) {
   const auto owner = run(lookup_table(*transaction_, code, scope, table_name));
   if (!owner) {
      return -1;
   }
   const auto row = run(transaction_->index<Index, by_primary>().find(owner->id, primary));
   if (!row) {
      return end(Kind, owner->id);
   }
   secondary = row->secondary;
   return cache(Kind, row->id.as_object_id(), row->table_id);
}

namespace {

template <typename T> bool equivalent(const T& left, const T& right) {
   return left == right;
}

bool equivalent(float64 left, float64 right) {
   return equal(left, right);
}

bool equivalent(float128 left, float128 right) {
   return equal(left, right);
}

std::uint64_t to_key(std::uint64_t value) {
   return value;
}

unsigned __int128 to_key(unsigned __int128 value) {
   return value;
}

float64 to_key(double value) {
   return float64{.bits = std::bit_cast<std::uint64_t>(value)};
}

float128 to_key(float128 value) {
   return value;
}

void from_key(std::uint64_t& output, std::uint64_t value) {
   output = value;
}

void from_key(unsigned __int128& output, unsigned __int128 value) {
   output = value;
}

void from_key(double& output, float64 value) {
   output = std::bit_cast<double>(value.bits);
}

void from_key(float128& output, float128 value) {
   output = value;
}

uint256 read_uint256(forge::vm::wasm::span<const unsigned __int128> value) {
   if (value.size() != 2U) {
      fail_database("idx256 data_len must equal two");
   }
   auto words = std::array<unsigned __int128, 2>{};
   std::memcpy(words.data(), value.data(), sizeof(words));
   return uint256::make_from_word_sequence(words[0], words[1]);
}

uint256 read_uint256(forge::vm::wasm::span<unsigned __int128> value) {
   return read_uint256(forge::vm::wasm::span<const unsigned __int128>{value.data(), value.size()});
}

void write_uint256(forge::vm::wasm::span<unsigned __int128> output, const uint256& value) {
   if (output.size() != 2U) {
      fail_database("idx256 data_len must equal two");
   }
   const auto words = value.get_array();
   std::memcpy(output.data(), words.data(), sizeof(words));
}

} // namespace

template <typename Row, typename Index, host::impl::row_kind Kind, typename Secondary>
std::int32_t host::impl::secondary_find_secondary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                                  const Secondary& secondary, std::uint64_t& primary) {
   require_ordered(secondary);
   const auto owner = run(lookup_table(*transaction_, code, scope, table_name));
   if (!owner) {
      return -1;
   }
   auto index = transaction_->index<Index, by_secondary>();
   const auto row = run(index.nth(run(index.lower_bound_rank(owner->id, secondary))));
   if (!same_table(row, owner->id) || !equivalent(row->secondary, secondary)) {
      return end(Kind, owner->id);
   }
   primary = row->primary;
   return cache(Kind, row->id.as_object_id(), row->table_id);
}

template <typename Row, typename Index, host::impl::row_kind Kind, typename Secondary>
std::int32_t host::impl::secondary_bound(bool upper, std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                         Secondary& secondary, std::uint64_t& primary) {
   require_ordered(secondary);
   const auto owner = run(lookup_table(*transaction_, code, scope, table_name));
   if (!owner) {
      return -1;
   }
   auto index = transaction_->index<Index, by_secondary>();
   const auto rank =
       upper ? run(index.upper_bound_rank(owner->id, secondary)) : run(index.lower_bound_rank(owner->id, secondary));
   const auto row = run(index.nth(rank));
   if (!same_table(row, owner->id)) {
      return end(Kind, owner->id);
   }
   primary = row->primary;
   secondary = row->secondary;
   return cache(Kind, row->id.as_object_id(), row->table_id);
}

template <typename Row, typename Index, host::impl::row_kind Kind>
std::int32_t host::impl::secondary_end(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name) {
   const auto owner = run(lookup_table(*transaction_, code, scope, table_name));
   return owner ? end(Kind, owner->id) : -1;
}

#define FORGE_CONTRACT_TEST_DEFINE_SECONDARY(prefix, row_type, index_type, kind_name, input_type, output_type)         \
   std::int32_t host::impl::prefix##_store(std::uint64_t scope, std::uint64_t table_name, std::uint64_t payer,         \
                                           std::uint64_t primary, input_type secondary) {                              \
      return secondary_store<row_type, index_type, row_kind::kind_name>(scope, table_name, payer, primary,             \
                                                                        to_key(*secondary.get()));                     \
   }                                                                                                                   \
   void host::impl::prefix##_update(std::int32_t iterator, std::uint64_t payer, input_type secondary) {                \
      secondary_update<row_type, index_type, row_kind::kind_name>(iterator, payer, to_key(*secondary.get()));          \
   }                                                                                                                   \
   void host::impl::prefix##_remove(std::int32_t iterator) {                                                           \
      secondary_remove<row_type, index_type, row_kind::kind_name>(iterator);                                           \
   }                                                                                                                   \
   std::int32_t host::impl::prefix##_next(std::int32_t iterator, output<std::uint64_t> primary) {                      \
      return secondary_next<row_type, index_type, row_kind::kind_name>(iterator, *primary.get());                      \
   }                                                                                                                   \
   std::int32_t host::impl::prefix##_previous(std::int32_t iterator, output<std::uint64_t> primary) {                  \
      return secondary_previous<row_type, index_type, row_kind::kind_name>(iterator, *primary.get());                  \
   }                                                                                                                   \
   std::int32_t host::impl::prefix##_find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,   \
                                                  output_type secondary, std::uint64_t primary) {                      \
      auto key = decltype(to_key(*secondary.get())){};                                                                 \
      const auto result =                                                                                              \
          secondary_find_primary<row_type, index_type, row_kind::kind_name>(code, scope, table_name, key, primary);    \
      if (result >= 0)                                                                                                 \
         from_key(*secondary.get(), key);                                                                              \
      return result;                                                                                                   \
   }                                                                                                                   \
   std::int32_t host::impl::prefix##_find_secondary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name, \
                                                    input_type secondary, output<std::uint64_t> primary) {             \
      return secondary_find_secondary<row_type, index_type, row_kind::kind_name>(                                      \
          code, scope, table_name, to_key(*secondary.get()), *primary.get());                                          \
   }                                                                                                                   \
   std::int32_t host::impl::prefix##_lowerbound(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,     \
                                                output_type secondary, output<std::uint64_t> primary) {                \
      auto key = to_key(*secondary.get());                                                                             \
      const auto result = secondary_bound<row_type, index_type, row_kind::kind_name>(false, code, scope, table_name,   \
                                                                                     key, *primary.get());             \
      if (result >= 0)                                                                                                 \
         from_key(*secondary.get(), key);                                                                              \
      (void)output_type(std::move(secondary));                                                                         \
      (void)output<std::uint64_t>(std::move(primary));                                                                 \
      return result;                                                                                                   \
   }                                                                                                                   \
   std::int32_t host::impl::prefix##_upperbound(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,     \
                                                output_type secondary, output<std::uint64_t> primary) {                \
      auto key = to_key(*secondary.get());                                                                             \
      const auto result = secondary_bound<row_type, index_type, row_kind::kind_name>(true, code, scope, table_name,    \
                                                                                     key, *primary.get());             \
      if (result >= 0)                                                                                                 \
         from_key(*secondary.get(), key);                                                                              \
      (void)output_type(std::move(secondary));                                                                         \
      (void)output<std::uint64_t>(std::move(primary));                                                                 \
      return result;                                                                                                   \
   }                                                                                                                   \
   std::int32_t host::impl::prefix##_end(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name) {          \
      return secondary_end<row_type, index_type, row_kind::kind_name>(code, scope, table_name);                        \
   }

FORGE_CONTRACT_TEST_DEFINE_SECONDARY(db_idx64, index64, index64_index, index64, input<std::uint64_t>,
                                     output<std::uint64_t>)
FORGE_CONTRACT_TEST_DEFINE_SECONDARY(db_idx128, index128, index128_index, index128, uint128_input, uint128_output)
FORGE_CONTRACT_TEST_DEFINE_SECONDARY(db_idx_double, index_double, index_double_index, index_double, input<double>,
                                     output<double>)
FORGE_CONTRACT_TEST_DEFINE_SECONDARY(db_idx_long_double, index_long_double, index_long_double_index, index_long_double,
                                     float128_input, float128_output)

#undef FORGE_CONTRACT_TEST_DEFINE_SECONDARY

std::int32_t host::impl::db_idx256_store(std::uint64_t scope, std::uint64_t table_name, std::uint64_t payer,
                                         std::uint64_t primary, input_span<unsigned __int128, 16> secondary) {
   return secondary_store<index256, index256_index, row_kind::index256>(scope, table_name, payer, primary,
                                                                        read_uint256(secondary));
}

void host::impl::db_idx256_update(std::int32_t iterator, std::uint64_t payer,
                                  input_span<unsigned __int128, 16> secondary) {
   secondary_update<index256, index256_index, row_kind::index256>(iterator, payer, read_uint256(secondary));
}

void host::impl::db_idx256_remove(std::int32_t iterator) {
   secondary_remove<index256, index256_index, row_kind::index256>(iterator);
}

std::int32_t host::impl::db_idx256_next(std::int32_t iterator, output<std::uint64_t> primary) {
   return secondary_next<index256, index256_index, row_kind::index256>(iterator, *primary.get());
}

std::int32_t host::impl::db_idx256_previous(std::int32_t iterator, output<std::uint64_t> primary) {
   return secondary_previous<index256, index256_index, row_kind::index256>(iterator, *primary.get());
}

std::int32_t host::impl::db_idx256_find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                                output_span<unsigned __int128, 16> secondary, std::uint64_t primary) {
   auto key = uint256{};
   const auto result =
       secondary_find_primary<index256, index256_index, row_kind::index256>(code, scope, table_name, key, primary);
   if (result >= 0) {
      write_uint256(secondary, key);
   }
   return result;
}

std::int32_t host::impl::db_idx256_find_secondary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                                  input_span<unsigned __int128, 16> secondary,
                                                  output<std::uint64_t> primary) {
   return secondary_find_secondary<index256, index256_index, row_kind::index256>(
       code, scope, table_name, read_uint256(secondary), *primary.get());
}

std::int32_t host::impl::db_idx256_lowerbound(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                              output_span<unsigned __int128, 16> secondary,
                                              output<std::uint64_t> primary) {
   auto key = read_uint256(secondary);
   const auto result = secondary_bound<index256, index256_index, row_kind::index256>(false, code, scope, table_name,
                                                                                     key, *primary.get());
   if (result >= 0) {
      write_uint256(secondary, key);
   }
   (void)output_span<unsigned __int128, 16>(std::move(secondary));
   (void)output<std::uint64_t>(std::move(primary));
   return result;
}

std::int32_t host::impl::db_idx256_upperbound(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                              output_span<unsigned __int128, 16> secondary,
                                              output<std::uint64_t> primary) {
   auto key = read_uint256(secondary);
   const auto result = secondary_bound<index256, index256_index, row_kind::index256>(true, code, scope, table_name, key,
                                                                                     *primary.get());
   if (result >= 0) {
      write_uint256(secondary, key);
   }
   (void)output_span<unsigned __int128, 16>(std::move(secondary));
   (void)output<std::uint64_t>(std::move(primary));
   return result;
}

std::int32_t host::impl::db_idx256_end(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name) {
   return secondary_end<index256, index256_index, row_kind::index256>(code, scope, table_name);
}

} // namespace forge::contract::testing
