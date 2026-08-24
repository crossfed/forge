#include "test_prelude.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <utility>

import forge.vm.wasm.interpret.backend;

#include "test_support.hpp"

#define FORGE_VM_WASM_INTERPRET_TEST_FILE watchdog_regression_tests

namespace {
class expires_on_guard_destruction {
 public:
   class guard {
    public:
      explicit guard(std::function<void()> callback) : _callback(std::move(callback)) {}
      ~guard() {
         _callback();
      }

    private:
      std::function<void()> _callback;
   };

   template <typename F> guard scoped_run(F&& callback) {
      return guard{std::forward<F>(callback)};
   }
};
} // namespace

TEST_CASE("expired watchdog runs callback before guard destruction", "[watchdog_expiration]") {
   auto expired = std::atomic_bool{false};
   auto timer = forge::vm::wasm::interpret::watchdog{std::chrono::milliseconds{0}};

   {
      auto guard = timer.scoped_run([&expired] { expired = true; });
   }

   BOOST_TEST(expired.load());
}

TEST_CASE("timed run reports expiry triggered during guard destruction", "[watchdog_expiration]") {
   namespace wasm = forge::vm::wasm::interpret;
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // header
       0x01, 0x04, 0x01, 0x60, 0x00, 0x00,             // one function type
       0x03, 0x02, 0x01, 0x00,                         // one function
       0x0a, 0x04, 0x01, 0x02, 0x00, 0x0b              // empty function body
   };
   auto memory = wasm::wasm_allocator{};

   {
      auto instance = wasm::backend<std::nullptr_t, wasm::interpreter>{code, &memory};
      BOOST_CHECK_THROW(instance.timed_run(expires_on_guard_destruction{}, [] {}), wasm::exceptions::timeout);
   }

   memory.free();
}
