#include "test_prelude.hpp"
import forge.vm.wasm.backend;
#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE guarded_ptr_tests

using namespace forge::vm::wasm;

struct S { int value; };

TEST_CASE("Testing guarded_ptr", "[guarded_ptr_tests]") {
   int a[10];
   {
      guarded_ptr<int> ptr(a, sizeof(a)/sizeof(a[0]));
      ptr += 10;
      BOOST_TEST(static_cast<bool>(ptr.raw() == a + 10));
   }
   {
      guarded_ptr<int> ptr(a, sizeof(a)/sizeof(a[0]));
      BOOST_CHECK_THROW(ptr += 11, exceptions::pointer_out_of_bounds);
   }
   {
      guarded_ptr<int> ptr(a, sizeof(a)/sizeof(a[0]));
      ptr += 0;
      BOOST_TEST(static_cast<bool>(ptr.raw() == a + 0));
   }
   // operator++
   {
      guarded_ptr<int> ptr(a, 1);
      auto& result = ++ptr;
      BOOST_TEST(static_cast<bool>(ptr.raw() == a + 1));
      BOOST_TEST(static_cast<bool>(&result == &ptr));
   }
   {
      guarded_ptr<int> ptr(a, 0);
      BOOST_CHECK_THROW(++ptr, exceptions::pointer_out_of_bounds);
   }
   {
      guarded_ptr<int> ptr(a, 1);
      auto result = ptr++;
      BOOST_TEST(static_cast<bool>(ptr.raw() == a + 1));
      BOOST_TEST(static_cast<bool>(result.raw() == a));
   }
   {
      guarded_ptr<int> ptr(a, 0);
      BOOST_CHECK_THROW(ptr++, exceptions::pointer_out_of_bounds);
   }
   // operator+
   {
      const guarded_ptr<int> ptr(a, 10);
      auto result = ptr + 10;
      BOOST_TEST(static_cast<bool>(result.raw() == a + 10));
   }
   {
      const guarded_ptr<int> ptr(a, 10);
      BOOST_CHECK_THROW(ptr + 11, exceptions::pointer_out_of_bounds);
   }
   {
      const guarded_ptr<int> ptr(a, 10);
      auto result = 10 + ptr;
      BOOST_TEST(static_cast<bool>(result.raw() == a + 10));
   }
   {
      const guarded_ptr<int> ptr(a, 10);
      BOOST_CHECK_THROW(11 + ptr, exceptions::pointer_out_of_bounds);
   }
   // operator*
   {
      const guarded_ptr<int> ptr(a, 1);
      *ptr = 42;
      BOOST_TEST(static_cast<bool>(a[0] == 42));
   }
   {
      const guarded_ptr<int> ptr(a, 0);
      BOOST_CHECK_THROW(*ptr, exceptions::pointer_out_of_bounds);
   }
   // operator->
   S s[10];
   {
      const guarded_ptr<S> ptr(s, 1);
      ptr->value = 42;
      BOOST_TEST(static_cast<bool>(s[0].value == 42));
   }
   {
      const guarded_ptr<S> ptr(s, 0);
      BOOST_CHECK_THROW(ptr->value, exceptions::pointer_out_of_bounds);
   }
   // at
   {
      const guarded_ptr<int> ptr(a, 1);
      a[0] = 42;
      BOOST_TEST(static_cast<bool>(ptr.at() == 42));
   }
   {
      const guarded_ptr<int> ptr(a, 0);
      BOOST_CHECK_THROW(ptr.at(), exceptions::pointer_out_of_bounds);
   }
   {
      const guarded_ptr<int> ptr(a, 10);
      a[9] = 43;
      BOOST_TEST(static_cast<bool>(ptr.at(9) == 43));
   }
   {
      const guarded_ptr<int> ptr(a, 10);
      BOOST_CHECK_THROW(ptr.at(10), exceptions::pointer_out_of_bounds);
   }
   // TODO: add_bounds/fit_bounds/bounds/offset
}
