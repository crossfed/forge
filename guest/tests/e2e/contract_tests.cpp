#define BOOST_TEST_MODULE forge_contract_e2e_tests

#include <boost/test/included/unit_test.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

import forge.chain.protocol.fixed_key;
import forge.chain.protocol.values;
import forge.contract.testing.host;
import forge.db.object.index;
import forge.raw.codec;
import forge.vm.wasm.backend;

namespace {

namespace protocol = forge::chain::protocol;
namespace wasm = forge::vm::wasm;

struct intrinsic_signature {
   std::vector<wasm::value_type> parameters;
   std::optional<wasm::value_type> result;

   bool operator==(const intrinsic_signature&) const = default;
};

wasm::value_type parse_value_type(std::string_view value) {
   if (value == "i32") {
      return wasm::i32;
   }
   if (value == "i64") {
      return wasm::i64;
   }
   throw std::runtime_error{"unsupported golden WASM value type"};
}

std::map<std::string, intrinsic_signature> read_database_intrinsic_golden() {
   auto input = std::ifstream{FORGE_CONTRACT_TEST_DB_GOLDEN};
   if (!input) {
      throw std::runtime_error{"cannot open database intrinsic golden fixture"};
   }

   auto result = std::map<std::string, intrinsic_signature>{};
   for (auto line = std::string{}; std::getline(input, line);) {
      if (line.empty() || line.front() == '#') {
         continue;
      }
      const auto first = line.find('|');
      const auto second = line.find('|', first + 1);
      if (first == std::string::npos || second == std::string::npos) {
         throw std::runtime_error{"invalid database intrinsic golden fixture"};
      }

      auto signature = intrinsic_signature{};
      auto parameters = std::string_view{line}.substr(first + 1, second - first - 1);
      while (!parameters.empty()) {
         const auto separator = parameters.find(',');
         signature.parameters.push_back(parse_value_type(parameters.substr(0, separator)));
         if (separator == std::string_view::npos) {
            break;
         }
         parameters.remove_prefix(separator + 1);
      }
      const auto return_type = std::string_view{line}.substr(second + 1);
      if (!return_type.empty()) {
         signature.result = parse_value_type(return_type);
      }

      const auto [_, inserted] = result.emplace(line.substr(0, first), std::move(signature));
      if (!inserted) {
         throw std::runtime_error{"duplicate database intrinsic golden entry"};
      }
   }
   return result;
}

std::string import_text(const wasm::guarded_vector<std::uint8_t>& value) {
   return {reinterpret_cast<const char*>(value.data()), value.size()};
}

class contract_abort : public std::runtime_error {
 public:
   using std::runtime_error::runtime_error;
};

struct invocation {
   std::vector<std::uint8_t> action_data;
   std::vector<std::uint8_t> return_value;

   void eosio_assert_message(std::uint32_t test, wasm::span<const char> message) {
      if (test == 0U) {
         throw contract_abort{std::string{message.data(), message.size()}};
      }
   }

   std::uint32_t action_data_size() const {
      return static_cast<std::uint32_t>(action_data.size());
   }

   std::uint32_t read_action_data(wasm::span<char> destination) const {
      const auto size = std::min(destination.size(), action_data.size());
      std::copy_n(action_data.begin(), size, destination.begin());
      return static_cast<std::uint32_t>(size);
   }

   void set_action_return_value(wasm::span<const char> value) {
      return_value.assign(reinterpret_cast<const std::uint8_t*>(value.data()),
                          reinterpret_cast<const std::uint8_t*>(value.data()) + value.size());
   }
};

using host_functions = wasm::registered_host_functions<invocation>;

void register_intrinsics() {
   static const auto registered = [] {
      host_functions::add<&invocation::eosio_assert_message>("env", "eosio_assert_message");
      host_functions::add<&invocation::action_data_size>("env", "action_data_size");
      host_functions::add<&invocation::read_action_data>("env", "read_action_data");
      host_functions::add<&invocation::set_action_return_value>("env", "set_action_return_value");
      return true;
   }();
   static_cast<void>(registered);
}

wasm::wasm_code read_contract(const std::filesystem::path& path) {
   auto input = std::ifstream{path, std::ios::binary | std::ios::ate};
   if (!input) {
      throw std::runtime_error{"cannot open contract fixture"};
   }
   const auto size = input.tellg();
   if (size < 0) {
      throw std::runtime_error{"cannot determine contract fixture size"};
   }
   auto result = wasm::wasm_code(static_cast<std::size_t>(size));
   input.seekg(0);
   if (!result.empty() && !input.read(reinterpret_cast<char*>(result.data()), size)) {
      throw std::runtime_error{"cannot read contract fixture"};
   }
   return result;
}

struct allocator {
   allocator(const allocator&) = delete;
   allocator& operator=(const allocator&) = delete;

   allocator() = default;
   ~allocator() {
      value.free();
   }

   wasm::wasm_allocator value;
};

template <typename implementation>
void apply_with(const wasm::wasm_code& code, invocation& host, std::string_view receiver,
                std::string_view first_receiver, std::string_view action) {
   static thread_local auto memory = allocator{};
   auto mutable_code = code;
   auto vm =
       wasm::backend<host_functions, implementation, wasm::compatibility_options>{mutable_code, host, &memory.value};
   vm(host, "env", "apply", protocol::make_name(receiver).value, protocol::make_name(first_receiver).value,
      protocol::make_name(action).value);
}

template <typename implementation>
void apply_with(const wasm::wasm_code& code, invocation& host, std::string_view contract, std::string_view action) {
   apply_with<implementation>(code, host, contract, contract, action);
}

void apply(const wasm::wasm_code& code, invocation& host, std::string_view receiver, std::string_view first_receiver,
           std::string_view action) {
   apply_with<wasm::interpreter>(code, host, receiver, first_receiver, action);
}

void apply(const wasm::wasm_code& code, invocation& host, std::string_view contract, std::string_view action) {
   apply_with<wasm::interpreter>(code, host, contract, action);
}

void run_allocator_action(std::string_view action) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_ALLOCATOR_WASM);
   auto host = invocation{};
   apply(code, host, "allocatortst", action);
}

forge::contract::testing::invocation_result invoke_database(forge::contract::testing::host& host,
                                                            const wasm::wasm_code& code, std::string_view receiver,
                                                            std::uint32_t scenario) {
   const auto account = protocol::make_name(receiver).value;
   return host.invoke({code.data(), code.size()}, account, account, protocol::make_name("run").value,
                      forge::raw::pack(scenario));
}

constexpr auto database_scope = std::uint64_t{1};

} // namespace

static_assert(forge::db::object::object_model<forge::contract::testing::table_index>);
static_assert(forge::db::object::object_model<forge::contract::testing::key_value_index>);
static_assert(forge::db::object::object_model<forge::contract::testing::index64_index>);
static_assert(forge::db::object::object_model<forge::contract::testing::index128_index>);
static_assert(forge::db::object::object_model<forge::contract::testing::index256_index>);
static_assert(forge::db::object::object_model<forge::contract::testing::index_double_index>);
static_assert(forge::db::object::object_model<forge::contract::testing::index_long_double_index>);
static_assert(
    std::is_same_v<decltype(forge::contract::testing::index128::secondary), forge::chain::protocol::fixed_key<16>>);

BOOST_AUTO_TEST_CASE(host_and_guest_share_action_argument_bytes) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_WASM);
   auto host = invocation{
       .action_data = forge::raw::pack(std::string{"alice"}, std::vector<std::uint32_t>{7U, 11U, 42U}),
   };

   const auto expected = std::vector<std::uint8_t>{
       0x05, 'a', 'l', 'i', 'c', 'e', 0x03, 0x07, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00,
   };
   BOOST_TEST(host.action_data == expected, boost::test_tools::per_element());
   BOOST_CHECK_NO_THROW(apply(code, host, "hello", "greet"));
}

BOOST_AUTO_TEST_CASE(host_and_guest_share_chain_value_bytes) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_WASM);
   const auto symbol = protocol::make_symbol("SYS", 4);
   auto host = invocation{
       .action_data = forge::raw::pack(protocol::make_name("eosio"), symbol, protocol::asset{42, symbol}),
   };

   const auto expected = std::vector<std::uint8_t>{
       0x00, 0x00, 0x00, 0x00, 0x00, 0xea, 0x30, 0x55, 0x04, 0x53, 0x59, 0x53, 0x00, 0x00, 0x00, 0x00,
       0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x53, 0x59, 0x53, 0x00, 0x00, 0x00, 0x00,
   };
   BOOST_TEST(host.action_data == expected, boost::test_tools::per_element());
   BOOST_CHECK_NO_THROW(apply(code, host, "hello", "values"));
}

BOOST_AUTO_TEST_CASE(wasm32_long_and_non_void_action_result_share_the_raw_codec) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_WASM);
   auto host = invocation{.action_data = forge::raw::pack(std::int32_t{20}, std::int32_t{22})};

   BOOST_CHECK_NO_THROW(apply(code, host, "hello", "add"));
   BOOST_TEST(host.return_value == forge::raw::pack(std::int32_t{42}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(generated_dispatcher_executes_zero_argument_action_with_empty_payload) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_WASM);
   auto host = invocation{};

   BOOST_CHECK_NO_THROW(apply(code, host, "hello", "answer"));
   BOOST_TEST(host.return_value == forge::raw::pack(std::uint32_t{42}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(generated_dispatcher_executes_const_action_method) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_WASM);
   auto host = invocation{};

   BOOST_CHECK_NO_THROW(apply(code, host, "hello", "constanswer"));
   BOOST_TEST(host.return_value == forge::raw::pack(std::uint32_t{43}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(generated_dispatcher_serializes_user_defined_action_records) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_RECORD_WASM);
   const auto record = forge::raw::pack(std::string{"alice"}, std::vector<std::uint32_t>{7U, 11U, 42U});
   auto host = invocation{.action_data = record};

   BOOST_CHECK_NO_THROW(apply(code, host, "recordtest", "echo"));
   BOOST_TEST(host.return_value == record, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(legacy_dispatcher_serializes_user_defined_action_records) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_LEGACY_WASM);
   const auto record = forge::raw::pack(std::string{"alice"}, std::vector<std::uint32_t>{7U, 11U, 42U});
   auto host = invocation{.action_data = record};

   BOOST_CHECK_NO_THROW(apply(code, host, "legacyhello", "echo"));
   BOOST_TEST(host.return_value == record, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(cdt_standard_containers_use_the_shared_guest_codec) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_WASM);
   auto host = invocation{.action_data = forge::raw::pack(
                              std::map<std::string, std::string>{{"answer", "42"}}, std::set<std::uint32_t>{2U, 1U},
                              std::deque<std::uint32_t>{3U, 4U}, std::list<std::uint32_t>{5U, 6U})};

   BOOST_CHECK_NO_THROW(apply(code, host, "hello", "containers"));
}

BOOST_AUTO_TEST_CASE(generated_and_legacy_dispatchers_ignore_foreign_notifications) {
   register_intrinsics();
   const auto modern_code = read_contract(FORGE_CONTRACT_TEST_WASM);
   const auto legacy_code = read_contract(FORGE_CONTRACT_TEST_LEGACY_WASM);
   auto modern_host = invocation{.action_data = forge::raw::pack(std::int32_t{20}, std::int32_t{22})};
   auto legacy_host = invocation{.action_data = {0x05, 'a'}};

   BOOST_CHECK_NO_THROW(apply(modern_code, modern_host, "hello", "foreign", "add"));
   BOOST_CHECK_NO_THROW(apply(legacy_code, legacy_host, "legacyhello", "foreign", "greet"));
   BOOST_TEST(modern_host.return_value.empty());
}

BOOST_AUTO_TEST_CASE(contract_check_failure_reaches_the_host) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_WASM);
   auto host = invocation{
       .action_data = forge::raw::pack(std::string{}, std::vector<std::uint32_t>{1U}),
   };

   BOOST_CHECK_EXCEPTION(apply(code, host, "hello", "greet"), contract_abort, [](const contract_abort& error) {
      return std::string_view{error.what()} == "user must not be empty";
   });
}

BOOST_AUTO_TEST_CASE(legacy_contract_uses_the_same_runtime_and_wire_codec) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_LEGACY_WASM);
   auto host = invocation{
       .action_data = forge::raw::pack(std::string{"legacy"}, std::vector<std::uint32_t>{1U, 2U}),
   };

   BOOST_CHECK_NO_THROW(apply(code, host, "legacyhello", "greet"));
}

BOOST_AUTO_TEST_CASE(cdt_database_fixture_imports_the_spring_database_interface) {
   auto code = read_contract(FORGE_CONTRACT_TEST_DB_WASM);
   using validator = wasm::backend<std::nullptr_t, wasm::null_backend, wasm::compatibility_options>;
   auto parsed = validator{code, static_cast<wasm::wasm_allocator*>(nullptr)};
   const auto& module = parsed.get_module();
   const auto expected = read_database_intrinsic_golden();
   auto actual = std::map<std::string, intrinsic_signature>{};

   for (std::uint32_t index = 0; index < module.imports.size(); ++index) {
      const auto& entry = module.imports[index];
      BOOST_TEST(entry.kind == wasm::external_kind::Function);
      BOOST_TEST(import_text(entry.module_str) == "env");

      const auto& type = module.get_function_type(index);
      auto signature = intrinsic_signature{};
      signature.parameters.assign(type.param_types.data(), type.param_types.data() + type.param_types.size());
      if (type.return_count != 0) {
         signature.result = type.return_type;
      }
      const auto [_, inserted] = actual.emplace(import_text(entry.field_str), std::move(signature));
      BOOST_TEST(inserted);
   }

   BOOST_TEST(actual.size() == 60U);
   BOOST_CHECK(actual == expected);
}

BOOST_AUTO_TEST_CASE(executable_database_fixture_imports_and_calls_the_full_spring_interface) {
   auto code = read_contract(FORGE_CONTRACT_TEST_DB_HOST_WASM);
   using validator = wasm::backend<std::nullptr_t, wasm::null_backend, wasm::compatibility_options>;
   auto parsed = validator{code, static_cast<wasm::wasm_allocator*>(nullptr)};
   const auto& module = parsed.get_module();
   const auto expected = read_database_intrinsic_golden();
   auto actual = std::map<std::string, intrinsic_signature>{};

   for (std::uint32_t index = 0; index < module.imports.size(); ++index) {
      const auto& entry = module.imports[index];
      const auto name = import_text(entry.field_str);
      if (!expected.contains(name)) {
         continue;
      }
      const auto& type = module.get_function_type(index);
      auto signature = intrinsic_signature{};
      signature.parameters.assign(type.param_types.data(), type.param_types.data() + type.param_types.size());
      if (type.return_count != 0) {
         signature.result = type.return_type;
      }
      actual.emplace(name, std::move(signature));
   }

   BOOST_TEST(actual.size() == 60U);
   BOOST_CHECK(actual == expected);
}

BOOST_AUTO_TEST_CASE(database_host_commits_primary_and_secondary_objectdb_state) {
   const auto code = read_contract(FORGE_CONTRACT_TEST_DB_HOST_WASM);
   auto host = forge::contract::testing::host{};
   const auto account = protocol::make_name("dbhost").value;

   BOOST_CHECK_NO_THROW(invoke_database(host, code, "dbhost", 0));
   const auto primary = host.find_primary(account, database_scope, 2, 30);
   BOOST_REQUIRE(primary.has_value());
   const auto expected = std::vector<std::uint8_t>{'u', 'p', 'd', 'a', 't', 'e', 'd', 0};
   BOOST_TEST(primary->value == expected, boost::test_tools::per_element());
   BOOST_TEST(primary->payer == account);
   const auto primary_table = host.find_table(account, database_scope, 2);
   BOOST_REQUIRE(primary_table.has_value());
   BOOST_TEST(primary_table->count == 1U);

   try {
      invoke_database(host, code, "dbhost", 1);
   } catch (const std::exception& error) {
      BOOST_FAIL("database donor scenario failed: " << error.what());
   }
   const auto row64 = host.find_index64(account, database_scope, 3, 30);
   const auto row128 = host.find_index128(account, database_scope, 4, 30);
   const auto row256 = host.find_index256(account, database_scope, 5, 30);
   const auto row_double = host.find_index_double(account, database_scope, 6, 30);
   const auto row_long_double = host.find_index_long_double(account, database_scope, 7, 30);
   BOOST_REQUIRE(row64.has_value());
   BOOST_REQUIRE(row128.has_value());
   BOOST_REQUIRE(row256.has_value());
   BOOST_REQUIRE(row_double.has_value());
   BOOST_REQUIRE(row_long_double.has_value());
   BOOST_TEST(row64->secondary == 50U);
   BOOST_TEST(static_cast<bool>(row128->secondary.get_array()[0] == static_cast<unsigned __int128>(50)));
   BOOST_TEST(static_cast<bool>(row256->secondary.get_array()[0] == static_cast<unsigned __int128>(50)));
   BOOST_TEST(row_double->secondary.bits == std::bit_cast<std::uint64_t>(50.0));
   BOOST_TEST(
       static_cast<bool>(row_long_double->secondary.words[0] != 0U || row_long_double->secondary.words[1] != 0U));
   BOOST_TEST(row64->payer == account);
   BOOST_TEST(row128->payer == account);
   BOOST_TEST(row256->payer == account);
   BOOST_TEST(row_double->payer == account);
   BOOST_TEST(row_long_double->payer == account);

   for (const auto table_name : {3U, 4U, 5U, 6U, 7U}) {
      const auto value = host.find_table(account, database_scope, table_name);
      BOOST_REQUIRE(value.has_value());
      BOOST_TEST(value->count == 1U);
   }

   BOOST_CHECK_NO_THROW(invoke_database(host, code, "dbhost", 17));
   const auto negative_zero = host.find_index_double(account, database_scope, 19, 10);
   const auto positive_zero = host.find_index_double(account, database_scope, 19, 20);
   BOOST_REQUIRE(negative_zero.has_value());
   BOOST_REQUIRE(positive_zero.has_value());
   BOOST_TEST(negative_zero->secondary.bits == std::bit_cast<std::uint64_t>(-0.0));
   BOOST_TEST(positive_zero->secondary.bits == std::bit_cast<std::uint64_t>(0.0));

   BOOST_CHECK_NO_THROW(invoke_database(host, code, "dbhost", 18));
   BOOST_TEST(!host.find_table(account, database_scope, 20).has_value());
}

BOOST_AUTO_TEST_CASE(multi_index_and_singleton_execute_over_the_objectdb_host) {
   const auto modern = read_contract(FORGE_CONTRACT_TEST_MULTI_INDEX_WASM);
   const auto legacy = read_contract(FORGE_CONTRACT_TEST_LEGACY_MULTI_INDEX_WASM);
   const auto modern_errors = read_contract(FORGE_CONTRACT_TEST_MULTI_INDEX_ERRORS_WASM);
   const auto legacy_errors = read_contract(FORGE_CONTRACT_TEST_LEGACY_MULTI_INDEX_ERRORS_WASM);
   const auto modern_extended = read_contract(FORGE_CONTRACT_TEST_MULTI_INDEX_EXTENDED_WASM);
   const auto legacy_extended = read_contract(FORGE_CONTRACT_TEST_LEGACY_MULTI_INDEX_EXTENDED_WASM);

   const auto exercise = [](const wasm::wasm_code& core, const wasm::wasm_code& errors, const wasm::wasm_code& extended,
                            std::string_view account_name) {
      auto host = forge::contract::testing::host{};
      const auto account = protocol::make_name(account_name).value;
      const auto table_name = protocol::make_name("records").value;
      const auto secondary_name = [table_name](std::uint64_t number) {
         return (table_name & 0xffff'ffff'ffff'fff0ULL) | number;
      };

      const auto invoke_success = [&](const wasm::wasm_code& code, std::uint32_t scenario) {
         const auto result = invoke_database(host, code, account_name, scenario);
         const auto expected = forge::raw::pack(scenario);
         BOOST_TEST(result.return_value == expected, boost::test_tools::per_element());
      };
      const auto donor_error = [](std::string_view expected) {
         return [expected](const forge::contract::testing::exceptions::assertion_failure& error) {
            return error.message() == expected;
         };
      };
      const auto invoke_failure = [&](const wasm::wasm_code& code, std::uint32_t scenario, std::string_view message) {
         BOOST_CHECK_EXCEPTION(invoke_database(host, code, account_name, scenario),
                               forge::contract::testing::exceptions::assertion_failure, donor_error(message));
      };

      invoke_success(core, 0);
      BOOST_REQUIRE(host.find_primary(account, account, table_name, 1).has_value());
      BOOST_REQUIRE(host.find_primary(account, account, table_name, 2).has_value());
      BOOST_REQUIRE(host.find_primary(account, account, table_name, 3).has_value());

      const auto index64 = host.find_index64(account, account, secondary_name(0), 1);
      const auto index128 = host.find_index128(account, account, secondary_name(1), 1);
      const auto index256 = host.find_index256(account, account, secondary_name(2), 1);
      const auto index_double = host.find_index_double(account, account, secondary_name(3), 1);
      const auto index_long_double = host.find_index_long_double(account, account, secondary_name(4), 1);
      BOOST_REQUIRE(index64.has_value());
      BOOST_REQUIRE(index128.has_value());
      BOOST_REQUIRE(index256.has_value());
      BOOST_REQUIRE(index_double.has_value());
      BOOST_REQUIRE(index_long_double.has_value());
      BOOST_TEST(index64->secondary == 20U);
      BOOST_TEST(static_cast<bool>(index128->secondary.get_array()[0] == static_cast<unsigned __int128>(200)));
      BOOST_TEST(static_cast<bool>(index256->secondary ==
                                   protocol::key256::make_from_word_sequence(std::uint64_t{0}, std::uint64_t{0},
                                                                             std::uint64_t{0}, std::uint64_t{20})));
      BOOST_TEST(index_double->secondary.bits == std::bit_cast<std::uint64_t>(2.0));

      invoke_success(core, 1);
      const auto modified = host.find_primary(account, account, table_name, 1);
      BOOST_REQUIRE(modified.has_value());
      BOOST_TEST(!host.find_primary(account, account, table_name, 2).has_value());
      const auto reindexed = host.find_index64(account, account, secondary_name(0), 1);
      BOOST_REQUIRE(reindexed.has_value());
      BOOST_TEST(reindexed->secondary == 5U);

      invoke_success(extended, 2);
      BOOST_TEST(host.find_table(account, account, protocol::make_name("settings").value).has_value());
      invoke_success(core, 3);
      invoke_success(core, 4);

      invoke_failure(core, 5, "cannot increment end iterator");
      invoke_failure(core, 6, "updater cannot change primary key when modifying an object");
      BOOST_TEST(host.find_primary(account, account, table_name, 1)->value == modified->value,
                 boost::test_tools::per_element());
      BOOST_TEST(!host.find_primary(account, account, table_name, 100).has_value());
      BOOST_TEST(host.find_index64(account, account, secondary_name(0), 1)->secondary == 5U);
      invoke_failure(core, 7, "object passed to iterator_to is not in multi_index");
      invoke_failure(core, 8, "rollback marker");
      BOOST_TEST(!host.find_primary(account, account, table_name, 10).has_value());

      const auto payer_before = host.find_primary(account, account, table_name, 1)->payer;
      invoke_success(core, 9);
      BOOST_TEST(host.find_primary(account, account, table_name, 1)->payer == payer_before);

      invoke_success(core, 33);
      BOOST_TEST(host.find_primary(account, account, table_name, 1)->payer == protocol::make_name("alice").value);
      BOOST_TEST(host.find_index64(account, account, secondary_name(0), 1)->payer == payer_before);

      invoke_failure(core, 10, "next primary key in table is at autoincrement limit");
      BOOST_TEST(!host.find_table(account, account, protocol::make_name("exhaust").value).has_value());

      constexpr auto failures = std::array{
          std::pair{11U, std::string_view{"unable to find key"}},
          std::pair{12U, std::string_view{"unable to find primary key in require_find"}},
          std::pair{13U, std::string_view{"unable to find secondary key"}},
          std::pair{14U, std::string_view{"unable to find sec key"}},
          std::pair{15U, std::string_view{"cannot decrement iterator at beginning of table"}},
          std::pair{16U, std::string_view{"cannot increment end iterator"}},
          std::pair{17U, std::string_view{"cannot decrement iterator at beginning of index"}},
          std::pair{18U, std::string_view{"cannot pass end iterator to modify"}},
          std::pair{19U, std::string_view{"cannot pass end iterator to erase"}},
          std::pair{20U, std::string_view{"cannot pass end iterator to modify"}},
          std::pair{21U, std::string_view{"cannot pass end iterator to erase"}},
          std::pair{22U, std::string_view{"object passed to iterator_to is not in multi_index"}},
          std::pair{23U, std::string_view{"object passed to modify is not in multi_index"}},
          std::pair{24U, std::string_view{"object passed to erase is not in multi_index"}},
          std::pair{30U, std::string_view{"object passed to iterator_to is not in multi_index"}},
          std::pair{31U, std::string_view{"object passed to iterator_to is not in multi_index"}},
      };
      for (const auto& [scenario, message] : failures) {
         invoke_failure(errors, scenario, message);
      }

      invoke_success(extended, 25);
      invoke_success(extended, 26);
      invoke_success(extended, 27);
      invoke_success(extended, 28);
      BOOST_TEST(!host.find_table(account, account, protocol::make_name("settings").value).has_value());
      invoke_failure(extended, 29, "singleton does not exist");
      invoke_success(extended, 32);
      const auto named_table = protocol::make_name("named").value;
      BOOST_REQUIRE(host.find_primary(account, account, named_table, protocol::make_name("alice").value).has_value());
      BOOST_REQUIRE(host.find_primary(account, account, named_table, protocol::make_name("bob").value).has_value());
      return host.snapshot();
   };

   const auto modern_state = exercise(modern, modern_errors, modern_extended, "midx");
   const auto legacy_state = exercise(legacy, legacy_errors, legacy_extended, "midx");
   BOOST_TEST(modern_state == legacy_state, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(database_host_rolls_back_assertions_and_commits_exit) {
   const auto code = read_contract(FORGE_CONTRACT_TEST_DB_HOST_WASM);
   auto host = forge::contract::testing::host{};
   const auto account = protocol::make_name("dbhost").value;

   BOOST_CHECK_THROW(invoke_database(host, code, "dbhost", 2), forge::contract::testing::exceptions::assertion_failure);
   BOOST_TEST(!host.find_table(account, database_scope, 8).has_value());

   const auto result = invoke_database(host, code, "dbhost", 3);
   BOOST_REQUIRE(result.exit_code.has_value());
   BOOST_TEST(*result.exit_code == 0);
   BOOST_TEST(host.find_primary(account, database_scope, 9, 77).has_value());
}

BOOST_AUTO_TEST_CASE(database_host_rejects_invalid_operations_without_partial_state) {
   const auto code = read_contract(FORGE_CONTRACT_TEST_DB_HOST_WASM);
   const auto account = protocol::make_name("dbhost").value;

   constexpr auto cases = std::array{
       std::pair{4U, 10U},  std::pair{5U, 11U},  std::pair{6U, 0U},   std::pair{8U, 12U},
       std::pair{9U, 13U},  std::pair{10U, 14U}, std::pair{12U, 16U}, std::pair{13U, 16U},
       std::pair{14U, 16U}, std::pair{15U, 17U}, std::pair{16U, 18U}, std::pair{19U, 21U},
   };
   for (const auto [scenario, table_name] : cases) {
      auto host = forge::contract::testing::host{};
      BOOST_CHECK_THROW(invoke_database(host, code, "dbhost", scenario), std::exception);
      if (table_name != 0U) {
         BOOST_TEST(!host.find_table(account, database_scope, table_name).has_value());
      }
   }

   auto duplicate_host = forge::contract::testing::host{};
   BOOST_CHECK_THROW(invoke_database(duplicate_host, code, "dbhost", 5),
                     forge::contract::testing::exceptions::database_error);
   BOOST_TEST(!duplicate_host.find_table(account, database_scope, 11).has_value());

   auto misaligned_host = forge::contract::testing::host{};
   BOOST_CHECK_NO_THROW(invoke_database(misaligned_host, code, "dbhost", 11));
   const auto misaligned = misaligned_host.find_index256(account, database_scope, 15, 1);
   BOOST_REQUIRE(misaligned.has_value());
   BOOST_TEST(static_cast<bool>(misaligned->secondary.get_array()[0] == static_cast<unsigned __int128>(42)));

   auto overflow_host = forge::contract::testing::host{};
   BOOST_CHECK_EXCEPTION(invoke_database(overflow_host, code, "dbhost", 20),
                         forge::contract::testing::exceptions::assertion_failure,
                         [](const forge::contract::testing::exceptions::assertion_failure& error) {
                            return error.message() == "raw signed varint overflows int32";
                         });
}

BOOST_AUTO_TEST_CASE(database_host_rejects_foreign_iterators_and_resets_iterator_cache) {
   const auto code = read_contract(FORGE_CONTRACT_TEST_DB_HOST_WASM);
   auto host = forge::contract::testing::host{};
   const auto account = protocol::make_name("dbhost").value;

   invoke_database(host, code, "dbhost", 0);
   BOOST_CHECK_THROW(invoke_database(host, code, "dbhost", 6), forge::contract::testing::exceptions::invalid_iterator);
   BOOST_CHECK_THROW(invoke_database(host, code, "foreign", 7), forge::contract::testing::exceptions::database_error);

   const auto row = host.find_primary(account, database_scope, 2, 30);
   BOOST_REQUIRE(row.has_value());
   const auto expected = std::vector<std::uint8_t>{'u', 'p', 'd', 'a', 't', 'e', 'd', 0};
   BOOST_TEST(row->value == expected, boost::test_tools::per_element());
}

#if defined(__x86_64__) || defined(_M_X64)
BOOST_AUTO_TEST_CASE(jit_executes_generated_contract) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_WASM);
   auto host = invocation{
       .action_data = forge::raw::pack(std::string{"jit"}, std::vector<std::uint32_t>{1U, 2U, 3U}),
   };

   BOOST_CHECK_NO_THROW(apply_with<wasm::jit>(code, host, "hello", "greet"));
}
#endif

BOOST_AUTO_TEST_CASE(malformed_action_data_is_rejected) {
   register_intrinsics();
   const auto code = read_contract(FORGE_CONTRACT_TEST_WASM);
   auto host = invocation{.action_data = {0x05, 'a'}};

   BOOST_CHECK_THROW(apply(code, host, "hello", "greet"), contract_abort);
}

BOOST_AUTO_TEST_CASE(cdt_allocator_allocates_wasm_memory) {
   run_allocator_action("donorpass");
}

BOOST_AUTO_TEST_CASE(cdt_allocator_respects_alignment) {
   run_allocator_action("donoralign");
}

BOOST_AUTO_TEST_CASE(contract_allocator_handles_first_normal_allocation_without_aligned_probe) {
   run_allocator_action("normalprobe");
}

BOOST_AUTO_TEST_CASE(contract_allocator_reallocates_and_zeroes_memory) {
   run_allocator_action("reallocates");
}

BOOST_AUTO_TEST_CASE(contract_allocator_grows_linear_memory) {
   run_allocator_action("grows");
}

BOOST_AUTO_TEST_CASE(contract_allocator_coalesces_free_blocks) {
   run_allocator_action("coalesces");
}

BOOST_AUTO_TEST_CASE(cdt_allocator_rejects_exhaustion) {
   BOOST_CHECK_EXCEPTION(run_allocator_action("donorfail"), contract_abort, [](const contract_abort& error) {
      return std::string_view{error.what()} == "failed to allocate pages";
   });
}

BOOST_AUTO_TEST_CASE(contract_allocator_rejects_overflow_without_mutation) {
   run_allocator_action("overflows");
}

BOOST_AUTO_TEST_CASE(contract_allocator_rejects_forged_aligned_metadata) {
   run_allocator_action("alignedguard");
}

BOOST_AUTO_TEST_CASE(contract_runtime_provides_guest_errno_storage) {
   run_allocator_action("errnovalue");
}

BOOST_AUTO_TEST_CASE(contract_runtime_provides_declared_string_api) {
   run_allocator_action("stringapi");
}

BOOST_AUTO_TEST_CASE(contract_runtime_memmove_handles_distinct_and_overlapping_objects) {
   run_allocator_action("memmoves");
}
