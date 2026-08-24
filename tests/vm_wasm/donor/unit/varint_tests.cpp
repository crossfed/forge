module;

#include "test_prelude.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

module forge.vm.wasm.interpret.backend;

import :leb128;
import forge.vm.wasm.interpret.allocator;
import forge.vm.wasm.interpret.stack_elem;
import forge.vm.wasm.interpret.utils;
import forge.vm.wasm.interpret.types;

#define FORGE_VM_WASM_INTERPRET_INTERNAL_TESTS
#include "test_support.hpp"

#define FORGE_VM_WASM_INTERPRET_TEST_FILE varint_tests

using namespace forge::vm::wasm::interpret;

TEST_CASE("Testing varuint", "[varuint_tests]") {
   {
    std::vector<uint8_t> tv = {0};
    guarded_ptr<uint8_t> gp1_0(tv.data(), 5);
    guarded_ptr<uint8_t> gp7_0(tv.data(), 5);
    guarded_ptr<uint8_t> gp32_0(tv.data(), 5);
    varuint<1> v1(gp1_0);
    varuint<7> v7(gp7_0);
    varuint<32> v32(gp32_0);

    BOOST_TEST(static_cast<bool>( v1.to() == 0 ));
    BOOST_TEST(static_cast<bool>( v7.to() == 0 ));
    BOOST_TEST(static_cast<bool>( v32.to() == 0 ));

    tv[0] = 1;
    guarded_ptr<uint8_t> gp1_1(tv.data(), 5);
    guarded_ptr<uint8_t> gp7_1(tv.data(), 5);
    guarded_ptr<uint8_t> gp32_1(tv.data(), 5);

    varuint<1> v1_1(gp1_1);
    varuint<7> v7_1(gp7_1);
    varuint<32> v32_1(gp32_1);
    BOOST_TEST(static_cast<bool>( v1_1.to() == 1 ));
    BOOST_TEST(static_cast<bool>( v7_1.to() == 1 ));
    BOOST_TEST(static_cast<bool>( v32_1.to() == 1 ));
   }

   {
    std::vector<uint8_t> tv = {0x7f};
    guarded_ptr<uint8_t> gp7_0(tv.data(), 5);
    guarded_ptr<uint8_t> gp32_0(tv.data(), 5);
    varuint<7> v7(gp7_0);
    varuint<32> v32(gp32_0);

    BOOST_TEST(static_cast<bool>( v7.to() ==  127 ));
    BOOST_TEST(static_cast<bool>( v32.to() == 127 ));

    tv[0] = 1;
    std::vector<uint8_t> tv2 = {0x80, 0x7f};
    guarded_ptr<uint8_t> gp7_1(tv2.data(), 5);
    guarded_ptr<uint8_t> gp32_1(tv2.data(), 5);

    varuint<32> v32_1(gp32_1);
    BOOST_TEST(static_cast<bool>( v32_1.to() == 16256 ));
   }

   {
    std::vector<uint8_t> tv0 = {0xb4, 0x7};
    guarded_ptr<uint8_t> gp32_0(tv0.data(), 5);

    std::vector<uint8_t> tv1 = {0x8c, 0x8};
    guarded_ptr<uint8_t> gp32_1(tv1.data(), 5);

    std::vector<uint8_t> tv2 = {0xff, 0xff, 0xff, 0xff, 0xf};
    guarded_ptr<uint8_t> gp32_2(tv2.data(), 5);

    varuint<32> v32_0(gp32_0);
    varuint<32> v32_1(gp32_1);
    varuint<32> v32_2(gp32_2);

    BOOST_TEST(static_cast<bool>( v32_0.to() == 0x3b4 ));
    BOOST_TEST(static_cast<bool>( v32_1.to() == 0x40c ));
    BOOST_TEST(static_cast<bool>( v32_2.to() == 0xffffffff ));
   }

   {
    varuint<32> v0((uint32_t)0);
    varuint<32> v1((uint32_t)2147483647);
    varuint<32> v2((uint32_t)4294967295);

    BOOST_TEST(static_cast<bool>( v0.to() == 0 ));
    BOOST_TEST(static_cast<bool>( v1.to() == 2147483647 ));
    BOOST_TEST(static_cast<bool>( v2.to() == 4294967295 ));
   }

}

TEST_CASE("Testing varint", "[varint_tests]") {
   {
    std::vector<uint8_t> tv0 = {0x0};
    std::vector<uint8_t> tv1 = {0x1};
    std::vector<uint8_t> tv2 = {0x7f};

    guarded_ptr<uint8_t> gp0(tv0.data(), 5);
    guarded_ptr<uint8_t> gp1(tv1.data(), 5);
    guarded_ptr<uint8_t> gp2(tv2.data(), 5);

    varint<7> v0(gp0);
    varint<7> v1(gp1);
    varint<7> v2(gp2);

    BOOST_TEST(static_cast<bool>( v0.to() == 0 ));
    BOOST_TEST(static_cast<bool>( v1.to() == 1 ));
    BOOST_TEST(static_cast<bool>( (int32_t)v2.to() == -1 ));
   }

   {
    std::vector<uint8_t> tv0 = {0x0};
    std::vector<uint8_t> tv1 = {0x1};
    std::vector<uint8_t> tv2 = {0x7f};
    std::vector<uint8_t> tv3 = {0x80, 0x7f};

    guarded_ptr<uint8_t> gp0(tv0.data(), 5);
    guarded_ptr<uint8_t> gp1(tv1.data(), 5);
    guarded_ptr<uint8_t> gp2(tv2.data(), 5);
    guarded_ptr<uint8_t> gp3(tv3.data(), 5);

    varint<32> v0(gp0);
    varint<32> v1(gp1);
    varint<32> v2(gp2);

    BOOST_TEST(static_cast<bool>( v0.to() == 0 ));
    BOOST_TEST(static_cast<bool>( v1.to() == 1 ));
    BOOST_TEST(static_cast<bool>( v2.to() == -1 ));
   }

   {
    std::vector<uint8_t> tv0 = {0x0};
    std::vector<uint8_t> tv1 = {0x1};
    std::vector<uint8_t> tv2 = {0x7f};

    guarded_ptr<uint8_t> gp0(tv0.data(), 5);
    guarded_ptr<uint8_t> gp1(tv1.data(), 5);
    guarded_ptr<uint8_t> gp2(tv2.data(), 5);

    varint<64> v0(gp0);
    varint<64> v1(gp1);
    varint<64> v2(gp2);

    BOOST_TEST(static_cast<bool>( v0.to() == 0 ));
    BOOST_TEST(static_cast<bool>( v1.to() == 1 ));
    BOOST_TEST(static_cast<bool>( v2.to() == -1 ));
   }

   {
    varint<32> v0((int32_t)0);
    varint<32> v1((int32_t)std::numeric_limits<int32_t>::min());
    varint<32> v2((int32_t)std::numeric_limits<int32_t>::max());

    varint<64> v3((int64_t)0);
    varint<64> v4((int64_t)std::numeric_limits<int64_t>::min());
    varint<64> v5((int64_t)std::numeric_limits<int64_t>::max());

    BOOST_TEST(static_cast<bool>( v0.to() == 0 ));
    BOOST_TEST(static_cast<bool>( v1.to() == std::numeric_limits<int32_t>::min() ));
    BOOST_TEST(static_cast<bool>( v2.to() == std::numeric_limits<int32_t>::max() ));

    BOOST_TEST(static_cast<bool>( v3.to() == 0 ));
    BOOST_TEST(static_cast<bool>( v4.to() == std::numeric_limits<int64_t>::min() ));
    BOOST_TEST(static_cast<bool>( v5.to() == std::numeric_limits<int64_t>::max() ));
   }
}
