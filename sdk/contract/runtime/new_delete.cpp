#include <__config>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>

import forge.contract.intrinsics;

_LIBCPP_BEGIN_NAMESPACE_STD
void __libcpp_verbose_abort(const char* message, ...) noexcept {
   forge::contract::abort(message == nullptr ? "libc++ contract runtime failure" : message);
}
_LIBCPP_END_NAMESPACE_STD

namespace {
void* allocate(std::size_t size) noexcept {
   return std::malloc(size == 0 ? 1 : size);
}

void* allocate_aligned(std::size_t size, std::align_val_t requested_alignment) noexcept {
   auto alignment = static_cast<std::size_t>(requested_alignment);
   if (alignment < alignof(void*)) {
      alignment = alignof(void*);
   }
   if ((alignment & (alignment - 1)) != 0 ||
       size > std::numeric_limits<std::size_t>::max() - alignment - sizeof(void*)) {
      return nullptr;
   }

   auto* allocation = std::malloc((size == 0 ? 1 : size) + alignment - 1 + sizeof(void*));
   if (allocation == nullptr) {
      return nullptr;
   }

   const auto base = reinterpret_cast<std::uintptr_t>(allocation) + sizeof(void*);
   const auto aligned = (base + alignment - 1) & ~(alignment - 1);
   auto* result = reinterpret_cast<void*>(aligned);
   reinterpret_cast<void**>(result)[-1] = allocation;
   return result;
}

[[noreturn]] void allocation_failed() {
   forge::contract::abort("contract allocation failed");
}

void deallocate_aligned(void* value) noexcept {
   if (value != nullptr) {
      std::free(reinterpret_cast<void**>(value)[-1]);
   }
}
} // namespace

void* operator new(std::size_t size) {
   if (auto* result = allocate(size)) {
      return result;
   }
   allocation_failed();
}

void* operator new[](std::size_t size) {
   return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
   return allocate(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
   return allocate(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
   if (auto* result = allocate_aligned(size, alignment)) {
      return result;
   }
   allocation_failed();
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
   return ::operator new(size, alignment);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
   return allocate_aligned(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
   return allocate_aligned(size, alignment);
}

void operator delete(void* value) noexcept {
   std::free(value);
}

void operator delete[](void* value) noexcept {
   std::free(value);
}

void operator delete(void* value, std::size_t) noexcept {
   std::free(value);
}

void operator delete[](void* value, std::size_t) noexcept {
   std::free(value);
}

void operator delete(void* value, const std::nothrow_t&) noexcept {
   std::free(value);
}

void operator delete[](void* value, const std::nothrow_t&) noexcept {
   std::free(value);
}

void operator delete(void* value, std::align_val_t) noexcept {
   deallocate_aligned(value);
}

void operator delete[](void* value, std::align_val_t) noexcept {
   deallocate_aligned(value);
}

void operator delete(void* value, std::size_t, std::align_val_t) noexcept {
   deallocate_aligned(value);
}

void operator delete[](void* value, std::size_t, std::align_val_t) noexcept {
   deallocate_aligned(value);
}

void operator delete(void* value, std::align_val_t, const std::nothrow_t&) noexcept {
   deallocate_aligned(value);
}

void operator delete[](void* value, std::align_val_t, const std::nothrow_t&) noexcept {
   deallocate_aligned(value);
}

extern "C" [[noreturn]] void abort() {
   forge::contract::abort("contract aborted");
}

extern "C" [[noreturn]] void __cxa_pure_virtual() {
   forge::contract::abort("pure virtual function called");
}
