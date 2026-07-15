#define BOOST_TEST_MODULE forge_contract_e2e_tests

#include <boost/test/included/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

class contract_abort : public std::runtime_error {
 public:
   using std::runtime_error::runtime_error;
};

struct invocation {
   std::vector<std::uint8_t> action_data;

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
};

using host_functions = wasm::registered_host_functions<invocation>;

void register_intrinsics() {
   static const auto registered = [] {
      host_functions::add<&invocation::eosio_assert_message>("env", "eosio_assert_message");
      host_functions::add<&invocation::action_data_size>("env", "action_data_size");
      host_functions::add<&invocation::read_action_data>("env", "read_action_data");
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
void apply_with(const wasm::wasm_code& code, invocation& host, std::string_view contract, std::string_view action) {
   static thread_local auto memory = allocator{};
   auto mutable_code = code;
   auto vm =
       wasm::backend<host_functions, implementation, wasm::compatibility_options>{mutable_code, host, &memory.value};
   vm(host, "env", "apply", protocol::make_name(contract).value, protocol::make_name(contract).value,
      protocol::make_name(action).value);
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
