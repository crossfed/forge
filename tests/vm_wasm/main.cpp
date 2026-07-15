#define BOOST_TEST_MODULE forge_vm_wasm
// Guarded guest memory owns fault signals, matching the donor's CATCH_CONFIG_NO_POSIX_SIGNALS policy.
#define BOOST_TEST_DISABLE_ALT_STACK
#include <boost/test/included/unit_test.hpp>

import forge.vm.wasm.allocator;

forge::vm::wasm::wasm_allocator wa;
