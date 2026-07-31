module;

#include <cstddef>
#include <new>

module forge.contract.multi_index;

namespace forge::contract::detail {

void* allocate_multi_index_storage(std::size_t size, std::size_t alignment) {
   if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
      return ::operator new(size, static_cast<std::align_val_t>(alignment));
   }
   return ::operator new(size);
}

void deallocate_multi_index_storage(void* storage, std::size_t alignment) noexcept {
   if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
      ::operator delete(storage, static_cast<std::align_val_t>(alignment));
      return;
   }
   ::operator delete(storage);
}

[[noreturn]] void fail_multi_index_allocation() {
   check(false, "multi_index allocation size overflow");
   __builtin_unreachable();
}

} // namespace forge::contract::detail
