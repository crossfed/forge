#include "test_prelude.hpp"
import forge.vm.wasm.interpret.allocator;
import forge.vm.wasm.interpret.stack_elem;
import forge.vm.wasm.interpret.utils;
import forge.vm.wasm.interpret.watchdog;
#include "test_support.hpp"

#define FORGE_VM_WASM_INTERPRET_TEST_FILE watchdog_tests

#include <atomic>
#include <chrono>

using forge::vm::wasm::interpret::watchdog;

TEST_CASE("watchdog interrupt", "[watchdog_interrupt]") {
  std::atomic<bool> okay = false;
  watchdog w{std::chrono::milliseconds(50)};
  {
    auto g = w.scoped_run([&]() { okay = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  BOOST_TEST(static_cast<bool>(okay));
}

TEST_CASE("watchdog no interrupt", "[watchdog_no_interrupt]") {
  std::atomic<bool> okay = true;
  watchdog w{std::chrono::milliseconds(50)};
  {
    auto g = w.scoped_run([&]() { okay = false; });
  } // the guard goes out of scope here, cancelling the timer
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  BOOST_TEST(static_cast<bool>(okay));
}
