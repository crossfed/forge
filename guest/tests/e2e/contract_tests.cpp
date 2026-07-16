#define BOOST_TEST_MODULE forge_contract_e2e_tests

#include <boost/test/included/unit_test.hpp>

#include <algorithm>
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
#include <vector>

import forge.chain.protocol.values;
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

} // namespace

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
