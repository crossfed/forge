#include "test_prelude.hpp"
import forge.vm.wasm.backend;
#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE allocator_tests

using namespace forge::vm::wasm;

template<typename T>
bool check_alignment(T* ptr) {
   void * p = ptr;
   std::size_t sz = sizeof(T);
   return std::align(alignof(T), sizeof(T), p, sz) != nullptr;
}

TEST_CASE("Testing growable_allocator alignment", "[growable_allocator]") {
   growable_allocator alloc(1024);
   unsigned char * cptr = alloc.alloc<unsigned char>(1);
   BOOST_TEST(static_cast<bool>(check_alignment(cptr)));
   uint16_t * sptr = alloc.alloc<uint16_t>(1);
   BOOST_TEST(static_cast<bool>(check_alignment(sptr)));
   uint32_t * iptr = alloc.alloc<uint32_t>(1);
   BOOST_TEST(static_cast<bool>(check_alignment(iptr)));
   uint64_t * lptr = alloc.alloc<uint64_t>(1);
   BOOST_TEST(static_cast<bool>(check_alignment(lptr)));
   *cptr = 0x11u;
   *sptr = 0x2233u;
   *iptr = 0x44556677u;
   *lptr = 0x8899102030405060u;
   BOOST_TEST(static_cast<bool>(*cptr == 0x11u));
   BOOST_TEST(static_cast<bool>(*sptr == 0x2233u));
   BOOST_TEST(static_cast<bool>(*iptr == 0x44556677u));
   BOOST_TEST(static_cast<bool>(*lptr == 0x8899102030405060u));
}

TEST_CASE("Testing maximum single allocation", "[growable_allocator]") {
   growable_allocator alloc(0);
   char * ptr = alloc.alloc<char>(0x40000000);
   ptr[0] = 'a';
   ptr[0x3FFFFFFF] = 'z';
   alloc.alloc<char>(0);
}


TEST_CASE("Testing maximum multiple allocation", "[growable_allocator]") {
   growable_allocator alloc(1024);
   for(int i = 0; i < 4; ++i) {
      char * ptr = alloc.alloc<char>(0x10000000);
      ptr[0] = 'a';
      ptr[0x0FFFFFFF] = 'z';
   }
   alloc.alloc<char>(0);
}

TEST_CASE("Testing too large single allocation", "[growable_allocator]") {
   growable_allocator alloc(1024);
   BOOST_CHECK_THROW(alloc.alloc<char>(0x40000001), exceptions::allocation);
}

TEST_CASE("Testing too large multiple allocation", "[growable_allocator]") {
   growable_allocator alloc(1024);
   alloc.alloc<char>(0x10000000);
   alloc.alloc<char>(0x10000000);
   alloc.alloc<char>(0x10000000);
   BOOST_CHECK_THROW(alloc.alloc<char>(0x10000001), exceptions::allocation);
}

TEST_CASE("Testing maximum initial size", "[growable_allocator]") {
   growable_allocator alloc(0x40000000);
   char * ptr = alloc.alloc<char>(0x40000000);
   ptr[0] = 'a';
   ptr[0x3FFFFFFF] = 'z';
}

TEST_CASE("Testing too large initial size", "[growable_allocator]") {
   BOOST_CHECK_THROW(growable_allocator{0x40000001}, exceptions::allocation);
   // Check that integer overflow in rounding functions won't cause issues
   BOOST_CHECK_THROW(growable_allocator{0x8000000000000000ull}, exceptions::allocation);
   BOOST_CHECK_THROW(growable_allocator{0xFFFFFFFFFFFE0001ull}, exceptions::allocation);
   BOOST_CHECK_THROW(growable_allocator{0xFFFFFFFFFFFFFFFFull}, exceptions::allocation);
}

TEST_CASE("Testing maximum aligned allocation", "[growable_allocator]") {
   growable_allocator alloc(1024);
   struct alignas(8) aligned_t { char a[8]; };
   alloc.alloc<char>(0x3FFFFFF4);
   aligned_t * ptr = alloc.alloc<aligned_t>(1);
   ptr->a[0] = 'a';
   ptr->a[7] = 'z';
   alloc.alloc<aligned_t>(0);
   BOOST_CHECK_THROW(alloc.alloc<char>(1), exceptions::allocation);
}

TEST_CASE("Testing reclaim", "[growable_allocator]") {
   growable_allocator alloc(1024);
   int * ptr1 = alloc.alloc<int>(10);
   alloc.reclaim(ptr1 + 2, 8);
   int * ptr2 = alloc.alloc<int>(10);
   BOOST_TEST(static_cast<bool>(ptr2 == ptr1 + 2));
}

TEST_CASE("Testing use_default_memory", "[growable_allocator]") {
   growable_allocator alloc(1024);
   // use_default_memory cannot be called when memory is already allocated by constructor
   BOOST_CHECK_THROW(alloc.use_default_memory(), exceptions::allocation);

   growable_allocator alloc1;
   alloc1.use_default_memory();
   // use_default_memory cannot be called multiple times
   BOOST_CHECK_THROW(alloc1.use_default_memory(), exceptions::allocation);

   growable_allocator alloc3;
   alloc3.use_default_memory();
   // can allocate as much as reserved memory
   alloc3.alloc<char>(growable_allocator::max_memory_size);
   // cannot allocate more than reserved memory
   BOOST_CHECK_THROW(alloc3.alloc<char>(1), exceptions::allocation);
}

TEST_CASE("Testing use_fixed_memory", "[growable_allocator]") {
   growable_allocator alloc(1024);
   // use_fixed_memory cannot be called when memory is already allocated by constructor
   BOOST_CHECK_THROW(alloc.use_fixed_memory(4096), exceptions::allocation);

   growable_allocator alloc1;
   alloc1.use_fixed_memory(1024);
   // use_fixed_memory cannot be called multiple times
   BOOST_CHECK_THROW(alloc1.use_fixed_memory(1024), exceptions::allocation);

   growable_allocator alloc2;
   // fixed_memory size cannot be 0
   BOOST_CHECK_THROW(alloc2.use_fixed_memory(0), exceptions::allocation);
   // fixed_memory size cannot be too big
   BOOST_CHECK_THROW(alloc2.use_fixed_memory(growable_allocator::max_memory_size + 1), exceptions::allocation);
   // fixed_memory size can be growable_allocator::max_memory_size
   alloc2.use_fixed_memory(growable_allocator::max_memory_size);

   growable_allocator alloc3;
   // reserved 1024 bytes
   alloc3.use_fixed_memory(1024);
   // can allocate less than reserved memory
   alloc3.alloc<char>(1000);
   // can allocate equal to reserved memory ( 1000+24 == 1024)
   alloc3.alloc<char>(24);
   // cannot allocate more than reserved memory ( 1000+24+1 > 1024)
   BOOST_CHECK_THROW(alloc3.alloc<char>(1), exceptions::allocation);
}

TEST_CASE("Testing mixed use_fixed_memory and alloc2.use_default_memory", "[growable_allocator]") {
   growable_allocator alloc1;
   alloc1.use_default_memory();
   // use_fixed_memory and use_fixed_memory cannot be mixed
   BOOST_CHECK_THROW(alloc1.use_fixed_memory(1024), exceptions::allocation);

   growable_allocator alloc2;
   alloc2.use_fixed_memory(1024);
   // use_fixed_memory and use_default_memory cannot be mixed
   BOOST_CHECK_THROW(alloc2.use_default_memory(), exceptions::allocation);
}
