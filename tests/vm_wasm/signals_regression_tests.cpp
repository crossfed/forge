module;

#include "test_prelude.hpp"

#include <array>
#include <cstddef>
#include <csignal>
#include <cstdint>
#include <limits>

module forge.vm.wasm.backend;

import :signals;
import forge.vm.wasm.allocator;

#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE signals_regression_tests

namespace wasm = forge::vm::wasm;

namespace {
struct reexport_host {
   static std::uint32_t read(wasm::argument_proxy<const char*> value) {
      return static_cast<std::uint32_t>(*value.get());
   }
};

struct escaped_signal {};

using reexport_host_functions = wasm::registered_host_functions<std::nullptr_t>;

volatile std::sig_atomic_t external_signal_count = 0;

void external_signal_handler(int) {
   external_signal_count = external_signal_count + 1;
}

class signal_action_guard {
 public:
   explicit signal_action_guard(int signal) : signal_{signal} {
      BOOST_REQUIRE(::sigaction(signal_, nullptr, &previous_) == 0);
   }

   ~signal_action_guard() {
      static_cast<void>(::sigaction(signal_, &previous_, nullptr));
   }

 private:
   int signal_;
   struct sigaction previous_{};
};

void register_reexport_host() {
   static const auto registered = [] {
      reexport_host_functions::add<&reexport_host::read>("host", "read");
      return true;
   }();
   static_cast<void>(registered);
}

template <typename Implementation> void check_reexported_host_import() {
   register_reexport_host();

   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // header
       0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f, // (i32) -> i32
       0x02, 0x0d, 0x01, 0x04, 0x68, 0x6f, 0x73, 0x74, 0x04, 0x72,
       0x65, 0x61, 0x64, 0x00, 0x00, 0x05, 0x03, 0x01, 0x00, 0x01, // one-page linear memory
       0x07, 0x08, 0x01, 0x04, 0x72, 0x65, 0x61, 0x64, 0x00, 0x00  // re-export imported read
   };
   using runtime = wasm::backend<reexport_host_functions, Implementation>;
   auto memory = wasm::wasm_allocator{};
   auto instance = runtime{code, &memory};

   const auto invoke = [&] {
      wasm::invoke_with_signal_handler([&] { instance("env", "read", std::numeric_limits<std::uint32_t>::max()); },
                                       [](int) { throw escaped_signal{}; }, nullptr, &memory);
   };

   BOOST_CHECK_THROW(invoke(), wasm::exceptions::memory);
   memory.free();
}
} // namespace

TEST_CASE("nested signal scopes restore the outer memory range", "[signals]") {
   auto outer_memory = wasm::wasm_allocator{};
   auto inner_memory = wasm::wasm_allocator{};

   wasm::invoke_with_signal_handler(
       [&] {
          const auto expected = wasm::memory_range;
          wasm::invoke_with_signal_handler([] {}, [](int) {}, nullptr, &inner_memory);

          BOOST_TEST(wasm::memory_range.data() == expected.data());
          BOOST_TEST(wasm::memory_range.size() == expected.size());
       },
       [](int) {}, nullptr, &outer_memory);

   outer_memory.free();
   inner_memory.free();
}

TEST_CASE("signal ranges classify addresses without pointer ordering", "[signals]") {
   auto bytes = std::array<std::byte, 8>{};
   const auto range = std::span<const std::byte>{bytes};
   const auto start = reinterpret_cast<std::uintptr_t>(range.data());

   BOOST_TEST(wasm::detail::contains_address(range, start));
   BOOST_TEST(wasm::detail::contains_address(range, start + range.size() - 1));
   BOOST_TEST(!wasm::detail::contains_address(range, start - 1));
   BOOST_TEST(!wasm::detail::contains_address(range, start + range.size()));
   BOOST_TEST(!wasm::detail::contains_address({}, start));
}

TEST_CASE("signal handlers are restored after an external replacement", "[signals]") {
   const auto guard = signal_action_guard{SIGFPE};
   wasm::setup_signal_handler();

   struct sigaction external{};
   external.sa_handler = &external_signal_handler;
   sigemptyset(&external.sa_mask);
   external.sa_flags = 0;
   BOOST_REQUIRE(::sigaction(SIGFPE, &external, nullptr) == 0);
   external_signal_count = 0;

   const auto invoke = [] {
      wasm::invoke_with_signal_handler([] { std::raise(SIGFPE); }, [](int) { throw escaped_signal{}; }, nullptr,
                                       nullptr);
   };
   BOOST_CHECK_THROW(invoke(), escaped_signal);
   BOOST_TEST(external_signal_count == 0);
}

TEST_CASE("interpreter re-exported host imports translate guest memory faults", "[signals]") {
   check_reexported_host_import<wasm::interpreter>();
}

#if FORGE_VM_WASM_HAS_JIT && !defined(FORGE_VM_WASM_TEST_INTERPRETER_ONLY)
TEST_CASE("jit re-exported host imports translate guest memory faults", "[signals]") {
   check_reexported_host_import<wasm::jit>();
}
#endif
