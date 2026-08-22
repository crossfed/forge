#define BOOST_TEST_MODULE forge_vm_wasm_interpret
// Guarded guest memory owns fault signals, matching the donor's CATCH_CONFIG_NO_POSIX_SIGNALS policy.
#define BOOST_TEST_DISABLE_ALT_STACK
#include <boost/test/included/unit_test.hpp>

import forge.vm.wasm.interpret.allocator;

forge::vm::wasm::interpret::wasm_allocator wa;
