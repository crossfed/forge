#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

import forge.contract;

class [[forge::contract("allocatortst")]] allocator_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void donorpass() {
      auto* byte = static_cast<volatile char*>(std::malloc(1));
      auto* word = static_cast<volatile short*>(std::malloc(sizeof(short)));
      auto* integer = static_cast<volatile int*>(std::malloc(sizeof(int)));
      auto* wide = static_cast<volatile long long*>(std::malloc(sizeof(long long)));
      auto* huge = static_cast<volatile __int128_t*>(std::malloc(sizeof(__int128_t)));

      *byte = 0x11;
      *word = 0x2222;
      *integer = 0x33333333;
      *wide = 0x4444444444444444;
      *huge = (static_cast<__int128_t>(0x5555555555555555) << 64) | 0x5555555555555555;

      forge::contract::check(*byte == 0x11, "wrong byte allocation");
      forge::contract::check(*word == 0x2222, "wrong short allocation");
      forge::contract::check(*integer == 0x33333333, "wrong integer allocation");
      forge::contract::check(*wide == 0x4444444444444444, "wrong wide allocation");
      forge::contract::check(*huge == ((static_cast<__int128_t>(0x5555555555555555) << 64) | 0x5555555555555555),
                             "wrong int128 allocation");
   }

   [[forge::action]] void donoralign() {
      check_malloc_alignment<short>();
      check_malloc_alignment<int>();
      check_malloc_alignment<long>();
      check_malloc_alignment<long long>();
      check_malloc_alignment<void*>();
      check_malloc_alignment<float>();
      check_malloc_alignment<double>();
      check_malloc_alignment<long double>();
      check_malloc_alignment<__int128_t>();

      struct alignas(64) cache_line {
         std::uint64_t value = 42U;
      };
      auto* aligned = new cache_line{};
      forge::contract::check(reinterpret_cast<std::uintptr_t>(aligned) % alignof(cache_line) == 0U,
                             "aligned new returned an unaligned pointer");
      forge::contract::check(aligned->value == 42U, "aligned new returned invalid storage");
      delete aligned;

      auto* bytes = static_cast<std::uint8_t*>(std::aligned_alloc(64U, 128U));
      forge::contract::check(bytes != nullptr, "aligned_alloc failed");
      forge::contract::check(reinterpret_cast<std::uintptr_t>(bytes) % 64U == 0U,
                             "aligned_alloc returned an unaligned pointer");
      bytes[0] = 0x5a;
      bytes[127] = 0xa5;
      auto* resized = static_cast<std::uint8_t*>(std::realloc(bytes, 256U));
      forge::contract::check(resized != nullptr && resized[0] == 0x5a && resized[127] == 0xa5,
                             "realloc lost aligned allocation data");
      std::free(resized);

      forge::contract::check(std::aligned_alloc(24U, 96U) == nullptr, "invalid alignment was accepted");
      forge::contract::check(std::aligned_alloc(64U, 65U) == nullptr, "invalid aligned size was accepted");
   }

   [[forge::action]] void reallocates() {
      constexpr auto original_size = std::size_t{1024U};
      auto* value = static_cast<std::uint8_t*>(std::malloc(original_size));
      forge::contract::check(value != nullptr, "initial allocation failed");
      for (auto index = std::size_t{0}; index < original_size; ++index) {
         value[index] = static_cast<std::uint8_t>(index);
      }

      value = static_cast<std::uint8_t*>(std::realloc(value, original_size * 2U));
      forge::contract::check(value != nullptr, "growing realloc failed");
      for (auto index = std::size_t{0}; index < original_size; ++index) {
         forge::contract::check(value[index] == static_cast<std::uint8_t>(index), "growing realloc lost data");
      }

      auto* guard = static_cast<std::uint8_t*>(std::malloc(256U));
      forge::contract::check(guard != nullptr, "realloc guard allocation failed");
      std::memset(guard, 0xa5, 256U);

      value = static_cast<std::uint8_t*>(std::realloc(value, original_size / 2U));
      forge::contract::check(value != nullptr, "shrinking realloc failed");
      for (auto index = std::size_t{0}; index < original_size / 2U; ++index) {
         forge::contract::check(value[index] == static_cast<std::uint8_t>(index), "shrinking realloc lost data");
      }
      for (auto index = std::size_t{0}; index < 256U; ++index) {
         forge::contract::check(guard[index] == 0xa5, "shrinking realloc corrupted its neighbor");
      }
      std::free(value);
      std::free(guard);

      auto* zeroed = static_cast<std::uint8_t*>(std::calloc(256U, 4U));
      forge::contract::check(zeroed != nullptr, "calloc failed");
      for (auto index = std::size_t{0}; index < 1024U; ++index) {
         forge::contract::check(zeroed[index] == 0U, "calloc returned non-zero storage");
      }
      std::free(zeroed);
   }

   [[forge::action]] void grows() {
      const auto before = __builtin_wasm_memory_size(0);
      auto* value = static_cast<std::uint8_t*>(std::malloc(256U * 1024U));
      forge::contract::check(value != nullptr, "allocation requiring memory growth failed");
      value[0] = 0x5a;
      value[256U * 1024U - 1U] = 0xa5;
      forge::contract::check(__builtin_wasm_memory_size(0) > before, "allocator did not grow linear memory");
      forge::contract::check(value[0] == 0x5a && value[256U * 1024U - 1U] == 0xa5, "grown allocation is not writable");
   }

   [[forge::action]] void coalesces() {
      constexpr auto chunk = std::size_t{1024U * 1024U};
      auto* first = std::malloc(chunk);
      auto* second = std::malloc(chunk);
      auto* third = std::malloc(chunk);
      auto* fourth = std::malloc(chunk);
      forge::contract::check(first != nullptr && second != nullptr && third != nullptr && fourth != nullptr,
                             "fragmentation setup failed");

      std::free(second);
      std::free(third);
      auto* merged = std::malloc(1536U * 1024U);
      forge::contract::check(merged == second, "allocator did not reuse coalesced free blocks");
   }

   [[forge::action]] void donorfail() {
      forge::contract::check(std::malloc(33U * 1024U * 1024U) != nullptr, "failed to allocate pages");
   }

 private:
   template <typename T> static void check_malloc_alignment() {
      auto* first = std::malloc(sizeof(T));
      forge::contract::check(first != nullptr, "alignment allocation failed");
      forge::contract::check(reinterpret_cast<std::uintptr_t>(first) % alignof(T) == 0U,
                             "malloc returned an unaligned pointer");
      static_cast<void>(std::malloc(1));
      auto* second = std::malloc(sizeof(T));
      forge::contract::check(second != nullptr, "second alignment allocation failed");
      forge::contract::check(reinterpret_cast<std::uintptr_t>(second) % alignof(T) == 0U,
                             "malloc returned an unaligned pointer after odd allocation");
   }
};
