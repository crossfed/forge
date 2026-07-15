module;

#include <cstddef>
#include <sys/mman.h>
#include <unistd.h>

module forge.vm.wasm.allocator;

namespace forge::vm::wasm {

stack_allocator::stack_allocator(std::size_t min_size) {
   if (min_size > 4 * 1024 * 1024) {
      const auto page_size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
      _size = ((min_size + page_size - 1) & ~(page_size - 1)) + 4 * 1024 * 1024;
      int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_STACK
      flags |= MAP_STACK;
#endif
      auto* ptr = ::mmap(nullptr, _size, PROT_READ | PROT_WRITE, flags, -1, 0);
      detail::check<exceptions::allocation>((ptr != MAP_FAILED), "failed to allocate alternate stack");
      _ptr = ptr;
   }
}

stack_allocator::~stack_allocator() {
   if (_ptr) {
      ::munmap(_ptr, _size);
   }
}

void* stack_allocator::top() const {
   if (_ptr) {
      return static_cast<char*>(_ptr) + _size;
   }
   return nullptr;
}

} // namespace forge::vm::wasm
