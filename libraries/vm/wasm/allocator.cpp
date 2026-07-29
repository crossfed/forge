module;

#include <cstddef>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

module forge.vm.wasm.allocator;

namespace forge::vm::wasm {

wasm_allocator::wasm_allocator() {
   const auto system_page_size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
   const auto mapping_size = max_memory + 2 * system_page_size;
   auto* mapping = static_cast<char*>(::mmap(nullptr, mapping_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
   detail::check<exceptions::allocation>((mapping != MAP_FAILED), "mmap failed to allocate pages");

   if (::mprotect(mapping, system_page_size, PROT_READ) != 0) {
      ::munmap(mapping, mapping_size);
      detail::fail<exceptions::allocation>("mprotect failed");
   }

   raw = mapping + system_page_size;
   page = 0;
}

wasm_allocator::~wasm_allocator() {
   free();
}

void wasm_allocator::free() noexcept {
   if (raw == nullptr)
      return;

   const auto system_page_size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
   auto* mapping = raw - system_page_size;
   raw = nullptr;
   page = -1;
   ::munmap(mapping, max_memory + 2 * system_page_size);
}

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

stack_allocator::stack_allocator(stack_allocator&& other) noexcept
    : _ptr(std::exchange(other._ptr, nullptr)), _size(std::exchange(other._size, 0)) {}

stack_allocator& stack_allocator::operator=(stack_allocator&& other) noexcept {
   if (this != &other) {
      release();
      _ptr = std::exchange(other._ptr, nullptr);
      _size = std::exchange(other._size, 0);
   }
   return *this;
}

stack_allocator::~stack_allocator() {
   release();
}

void stack_allocator::release() noexcept {
   if (_ptr) {
      ::munmap(_ptr, _size);
      _ptr = nullptr;
      _size = 0;
   }
}

void* stack_allocator::top() const {
   if (_ptr) {
      return static_cast<char*>(_ptr) + _size;
   }
   return nullptr;
}

contiguous_allocator::contiguous_allocator(std::size_t size) : _size(align_to_page(size)) {
   _base = static_cast<char*>(::mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
   detail::check<exceptions::allocation>((_base != MAP_FAILED), "mmap failed.");
}

contiguous_allocator::contiguous_allocator(contiguous_allocator&& other) noexcept
    : _offset(std::exchange(other._offset, 0)), _size(std::exchange(other._size, 0)),
      _base(std::exchange(other._base, nullptr)) {}

contiguous_allocator& contiguous_allocator::operator=(contiguous_allocator&& other) noexcept {
   if (this != &other) {
      release();
      _offset = std::exchange(other._offset, 0);
      _size = std::exchange(other._size, 0);
      _base = std::exchange(other._base, nullptr);
   }
   return *this;
}

contiguous_allocator::~contiguous_allocator() {
   release();
}

void contiguous_allocator::release() noexcept {
   if (_base) {
      ::munmap(_base, _size);
      _offset = 0;
      _size = 0;
      _base = nullptr;
   }
}

} // namespace forge::vm::wasm
