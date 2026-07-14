#include "test_prelude.hpp"

#include <atomic>
#include <chrono>

import forge.vm.wasm.backend;

#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE watchdog_regression_tests

TEST_CASE("expired watchdog runs callback before guard destruction", "[watchdog_expiration]") {
   auto expired = std::atomic_bool{false};
   auto timer = forge::vm::wasm::watchdog{std::chrono::milliseconds{0}};

   {
      auto guard = timer.scoped_run([&expired] { expired = true; });
   }

   BOOST_TEST(expired.load());
}
