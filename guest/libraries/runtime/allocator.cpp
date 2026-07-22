#include <algorithm>
#include "details/allocator.hxx"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#define CURRENT_MEMORY __builtin_wasm_memory_size(0)
#define GROW_MEMORY(pages) __builtin_wasm_memory_grow(0, pages)

namespace forge::contract::runtime {
struct aligned_allocation_header;
aligned_allocation_header* aligned_header(void* ptr);

void* sbrk(size_t num_bytes) {
   constexpr size_t NBPPL2 = 16U;
   constexpr size_t NBBP = 65536U;

   static bool initialized;
   static size_t sbrk_bytes;
   if (!initialized) {
      sbrk_bytes = CURRENT_MEMORY * NBBP;
      initialized = true;
   }

   if (num_bytes > INT32_MAX || num_bytes > std::numeric_limits<size_t>::max() - 7U)
      return reinterpret_cast<void*>(-1);

   const size_t prev_num_bytes = sbrk_bytes;
   const size_t current_pages = CURRENT_MEMORY;

   // round the absolute value of num_bytes to an alignment boundary
   num_bytes = (num_bytes + 7U) & ~7U;

   if (num_bytes > std::numeric_limits<size_t>::max() - sbrk_bytes)
      return reinterpret_cast<void*>(-1);

   // update the number of bytes allocated, and compute the number of pages needed
   const size_t desired_bytes = sbrk_bytes + num_bytes;
   const size_t num_desired_pages = (desired_bytes >> NBPPL2) + ((desired_bytes & (NBBP - 1U)) != 0U);

   if (num_desired_pages > current_pages) {
      if (GROW_MEMORY(num_desired_pages - current_pages) == -1)
         return reinterpret_cast<void*>(-1);
   }

   sbrk_bytes += num_bytes;
   return reinterpret_cast<void*>(prev_num_bytes);
}

using ::memcpy;
using ::memset;

class memory_manager // NOTE: Should never allocate another instance of memory_manager
{
   friend void* ::malloc(size_t size);
   friend void* ::aligned_alloc(size_t alignment, size_t size);
   friend void* ::calloc(size_t count, size_t size);
   friend void* ::realloc(void* ptr, size_t size);
   friend void ::free(void* ptr);
   friend aligned_allocation_header* aligned_header(void* ptr);

 public:
   memory_manager()
       // NOTE: it appears that WASM has an issue with initialization lists if the object is globally allocated,
       //       and seems to just initialize members to 0
       : _heaps_actual_size(0), _active_heap(0) {
      // eosio::print("HEAP : ", __data_end, '\n');
   }

 private:
   class memory;

   memory* next_active_heap() {
      constexpr size_t wasm_page_size = 64 * 1024;
      memory* const current_memory = _available_heaps + _active_heap;

      auto* const current_break = sbrk(0);
      if (current_break == reinterpret_cast<void*>(-1))
         return nullptr;
      const size_t current_memory_size = reinterpret_cast<size_t>(current_break);

      // grab up to the end of the current WASM memory page provided that it has 1KiB remaining, otherwise
      //  grow to end of next page
      const auto page_remainder = current_memory_size % wasm_page_size;
      const auto page_remaining = wasm_page_size - page_remainder;
      const auto heap_adj = page_remaining >= 1024U ? page_remaining : page_remaining + wasm_page_size;
      char* new_memory_start = reinterpret_cast<char*>(sbrk(heap_adj));
      if (new_memory_start == reinterpret_cast<char*>(-1)) {
         // ensure that any remaining unallocated memory gets cleaned up
         current_memory->cleanup_remaining();
         ++_active_heap;
         _heaps_actual_size = _active_heap;
         return nullptr;
      }

      // if we can expand the current memory, keep working with it
      if (current_memory->expand_memory(new_memory_start, heap_adj))
         return current_memory;

      // ensure that any remaining unallocated memory gets cleaned up
      current_memory->cleanup_remaining();

      ++_active_heap;
      memory* const next = _available_heaps + _active_heap;
      next->init(new_memory_start, heap_adj);

      return next;
   }
   void* malloc(size_t size) {
      if (size == 0)
         return nullptr;

      // see Note on ctor
      if (_heaps_actual_size == 0)
         _heaps_actual_size = _heaps_size;

      if (!adjust_to_mem_block(size))
         return nullptr;

      const auto reuse_freed = [&]() -> char* {
         const auto heap_count = std::min(_active_heap + 1U, _heaps_actual_size);
         for (auto index = size_t{0}; index < heap_count; ++index) {
            auto& heap = _available_heaps[index];
            if (heap.is_init()) {
               if (auto* result = heap.malloc_from_freed(size)) {
                  return result;
               }
            }
         }
         return nullptr;
      };

      // first pass of loop never has to initialize the slot in _available_heap
      char* buffer = nullptr;
      memory* current = nullptr;
      // need to make sure
      if (_active_heap < _heaps_actual_size) {
         memory* const start_heap = &_available_heaps[_active_heap];
         // only heap 0 won't be initialized already
         if (_active_heap == 0 && !start_heap->is_init()) {
            start_heap->init(_initial_heap, _initial_heap_size);
         }

         current = start_heap;
      }

      while (current != nullptr) {
         buffer = current->malloc(size);
         // done if we have a buffer
         if (buffer != nullptr)
            break;

         buffer = reuse_freed();
         if (buffer != nullptr)
            break;

         current = next_active_heap();
      }

      if (buffer == nullptr) {
         buffer = reuse_freed();
      }

      return buffer;
   }

   void* realloc(void* ptr, size_t size) {
      if (size == 0) {
         free(ptr);
         return nullptr;
      }

      if (!adjust_to_mem_block(size))
         return nullptr;

      char* realloc_ptr = nullptr;
      size_t orig_ptr_size = 0;
      if (ptr != nullptr) {
         char* const char_ptr = static_cast<char*>(ptr);
         for (memory* realloc_heap = _available_heaps;
              realloc_heap < _available_heaps + _heaps_actual_size && realloc_heap->is_init(); ++realloc_heap) {
            if (realloc_heap->is_in_heap(char_ptr)) {
               realloc_ptr = realloc_heap->realloc_in_place(char_ptr, size, &orig_ptr_size);

               if (realloc_ptr != nullptr)
                  return realloc_ptr;
               else
                  break;
            }
         }
      }

      char* new_alloc = static_cast<char*>(malloc(size));
      if (new_alloc == nullptr)
         return nullptr;

      const size_t copy_size = (size < orig_ptr_size) ? size : orig_ptr_size;
      if (copy_size > 0) {
         memcpy(new_alloc, ptr, copy_size);
         free(ptr);
      }

      return new_alloc;
   }

   void free(void* ptr) {
      if (ptr == nullptr)
         return;

      char* const char_ptr = static_cast<char*>(ptr);
      for (memory* free_heap = _available_heaps;
           free_heap < _available_heaps + _heaps_actual_size && free_heap->is_init(); ++free_heap) {
         if (free_heap->is_in_heap(char_ptr)) {
            free_heap->free(char_ptr);
            break;
         }
      }
   }

   bool mark_aligned(void* ptr) {
      if (ptr == nullptr)
         return false;

      auto* const candidate = static_cast<char*>(ptr);
      for (memory* heap = _available_heaps; heap < _available_heaps + _heaps_actual_size && heap->is_init(); ++heap) {
         if (heap->mark_aligned(candidate))
            return true;
      }
      return false;
   }

   bool aligned_allocation_size(const void* ptr, size_t& size) const {
      if (ptr == nullptr)
         return false;

      const auto* const candidate = static_cast<const char*>(ptr);
      for (const memory* heap = _available_heaps; heap < _available_heaps + _heaps_actual_size && heap->is_init();
           ++heap) {
         bool aligned = false;
         if (heap->allocation(candidate, size, aligned))
            return aligned;
      }
      return false;
   }

   static bool adjust_to_mem_block(size_t& size) {
      constexpr auto maximum_size = _alloc_memory_mask - _mem_block;
      if (size > maximum_size)
         return false;

      size = (size + _rem_mem_block_mask) & ~_rem_mem_block_mask;
      return true;
   }

   class memory {
    public:
      memory() : _heap_size(0), _heap(nullptr), _offset(0) {}

      void init(char* const mem_heap, size_t size) {
         _heap_size = size;
         _heap = mem_heap;
      }

      size_t is_init() const {
         return _heap != nullptr;
      }

      size_t is_in_heap(const char* const ptr) const {
         const char* const end_of_buffer = _heap + _heap_size;
         const char* const first_ptr_of_buffer = _heap + _size_marker;
         return ptr >= first_ptr_of_buffer && ptr < end_of_buffer;
      }

      size_t is_capacity_remaining() const {
         return _offset < _heap_size && _size_marker < _heap_size - _offset;
      }

      char* malloc(size_t size) {
         if (_offset > _heap_size || size > _heap_size - _offset || _size_marker > _heap_size - _offset - size) {
            return nullptr;
         }

         const size_t used_up_size = _offset + size + _size_marker;
         buffer_ptr new_buff(&_heap[_offset + _size_marker], size, _heap + _heap_size);
         _offset = used_up_size;
         new_buff.mark_alloc();
         return new_buff.ptr();
      }

      char* malloc_from_freed(size_t size) {
         const auto* initialized_end = _heap + _offset;
         char* current = _heap + _size_marker;
         while (current != nullptr && current < initialized_end) {
            buffer_ptr current_buffer(current, initialized_end);
            if (!current_buffer.is_alloc()) {
               // done if we have enough contiguous memory
               if (current_buffer.merge_contiguous(size)) {
                  current_buffer.mark_alloc();
                  return current;
               }
            }

            current = current_buffer.next_ptr();
         }

         // failed to find any free memory
         return nullptr;
      }

      char* realloc_in_place(char* const ptr, size_t size, size_t* orig_ptr_size) {
         const char* const end_of_buffer = _heap + _heap_size;

         buffer_ptr orig_buffer(ptr, end_of_buffer);
         *orig_ptr_size = orig_buffer.size();
         // is the passed in pointer valid
         char* const orig_buffer_end = orig_buffer.end();
         if (orig_buffer_end > end_of_buffer) {
            *orig_ptr_size = 0;
            return nullptr;
         }

         if (size > static_cast<size_t>(end_of_buffer - ptr)) {
            // cannot resize in place
            return nullptr;
         }

         if (size == *orig_ptr_size)
            return ptr;

         // The last allocation can move the bump boundary in either direction without creating a free record.
         if (orig_buffer_end == &_heap[_offset]) {
            orig_buffer.size(size);
            if (size > *orig_ptr_size) {
               _offset += size - *orig_ptr_size;
            } else {
               _offset -= *orig_ptr_size - size;
            }

            return ptr;
         }

         if (*orig_ptr_size > size) {
            const auto remainder = *orig_ptr_size - size;
            if (remainder <= _size_marker)
               return ptr;

            orig_buffer.size(size);
            char* const free_ptr = ptr + size + _size_marker;
            buffer_ptr excess_to_free(free_ptr, remainder - _size_marker, _heap + _heap_size);
            excess_to_free.mark_free();
            return ptr;
         }

         if (!orig_buffer.merge_contiguous_if_available(size))
            // could not resize in place
            return nullptr;

         orig_buffer.mark_alloc();
         return ptr;
      }

      void free(char* ptr) {
         buffer_ptr to_free(ptr, _heap + _heap_size);
         to_free.mark_free();
      }

      bool mark_aligned(char* ptr) {
         size_t size = 0;
         bool aligned = false;
         if (!allocation(ptr, size, aligned))
            return false;

         buffer_ptr block(ptr, _heap + _offset);
         block.mark_aligned();
         return true;
      }

      bool allocation(const char* ptr, size_t& size, bool& aligned) const {
         const char* const initialized_end = _heap + _offset;
         char* current = _heap + _size_marker;
         while (current != nullptr && current < initialized_end) {
            buffer_ptr block(current, initialized_end);
            if (current == ptr) {
               if (!block.is_alloc() || block.end() > initialized_end)
                  return false;
               size = block.size();
               aligned = block.is_aligned();
               return true;
            }
            current = block.next_ptr();
         }
         return false;
      }

      void cleanup_remaining() {
         if (_offset == _heap_size)
            return;

         // take the remaining memory and act like it was allocated
         const size_t size = _heap_size - _offset - _size_marker;
         buffer_ptr new_buff(&_heap[_offset + _size_marker], size, _heap + _heap_size);
         _offset = _heap_size;
         new_buff.mark_free();
      }

      bool expand_memory(char* exp_mem, size_t size) {
         if (_heap + _heap_size != exp_mem)
            return false;

         _heap_size += size;

         return true;
      }

    private:
      class buffer_ptr {
       public:
         buffer_ptr(void* ptr, const char* const heap_end)
             : _ptr(static_cast<char*>(ptr)),
               _size(*reinterpret_cast<size_t*>(static_cast<char*>(ptr) - _size_marker) & ~_memory_state_mask),
               _heap_end(heap_end) {}

         buffer_ptr(void* ptr, size_t buff_size, const char* const heap_end)
             : _ptr(static_cast<char*>(ptr)), _heap_end(heap_end) {
            memset(_ptr - _size_marker, 0, _size_marker);
            size(buff_size);
         }

         size_t size() const {
            return _size;
         }

         char* next_ptr() const {
            char* const next = end() + _size_marker;
            if (next >= _heap_end)
               return nullptr;

            return next;
         }

         void size(size_t val) {
            // keep the same state (allocated or free) as was set before
            const size_t memory_state = *reinterpret_cast<size_t*>(_ptr - _size_marker) & _memory_state_mask;
            *reinterpret_cast<size_t*>(_ptr - _size_marker) = val | memory_state;
            _size = val;
         }

         char* end() const {
            return _ptr + _size;
         }

         char* ptr() const {
            return _ptr;
         }

         void mark_alloc() {
            auto& marker = *reinterpret_cast<size_t*>(_ptr - _size_marker);
            marker = (marker & ~_memory_state_mask) | _alloc_memory_mask;
         }

         void mark_free() {
            *reinterpret_cast<size_t*>(_ptr - _size_marker) &= ~_memory_state_mask;
         }

         void mark_aligned() {
            *reinterpret_cast<size_t*>(_ptr - _size_marker) |= _aligned_memory_mask;
         }

         bool is_alloc() const {
            return *reinterpret_cast<const size_t*>(_ptr - _size_marker) & _alloc_memory_mask;
         }

         bool is_aligned() const {
            return *reinterpret_cast<const size_t*>(_ptr - _size_marker) & _aligned_memory_mask;
         }

         bool merge_contiguous_if_available(size_t needed_size) {
            return merge_contiguous(needed_size, true);
         }

         bool merge_contiguous(size_t needed_size) {
            return merge_contiguous(needed_size, false);
         }

       private:
         bool merge_contiguous(size_t needed_size, bool all_or_nothing) {
            // do not bother if there isn't contiguious space to allocate
            if (all_or_nothing && size_t(_heap_end - _ptr) < needed_size)
               return false;

            size_t possible_size = _size;
            while (possible_size < needed_size && (_ptr + possible_size < _heap_end)) {
               const size_t next_mem_flag_size = *reinterpret_cast<const size_t*>(_ptr + possible_size);
               // if ALLOCed then done with contiguous free memory
               if (next_mem_flag_size & _alloc_memory_mask)
                  break;

               possible_size += (next_mem_flag_size & ~_memory_state_mask) + _size_marker;
            }

            if (all_or_nothing && possible_size < needed_size)
               return false;

            // combine
            const size_t new_size = possible_size < needed_size ? possible_size : needed_size;
            size(new_size);

            if (possible_size > needed_size) {
               const size_t freed_size = possible_size - needed_size - _size_marker;
               buffer_ptr freed_remainder(_ptr + needed_size + _size_marker, freed_size, _heap_end);
               freed_remainder.mark_free();
            }

            return new_size == needed_size;
         }

         char* _ptr;
         size_t _size;
         const char* const _heap_end;
      };

      size_t _heap_size;
      char* _heap;
      size_t _offset;
   };

   static constexpr size_t _mem_block = alignof(std::max_align_t);
   static constexpr size_t _size_marker = _mem_block;
   static const size_t _rem_mem_block_mask = _mem_block - 1;
   static const size_t _initial_heap_size = 8192; // 32768;
   // if sbrk is not called outside of this file, then this is the max times we can call it
   static const size_t _heaps_size = 16;
   static const size_t _aligned_memory_mask = 1U;
   alignas(std::max_align_t) char _initial_heap[_initial_heap_size];
   memory _available_heaps[_heaps_size];
   size_t _heaps_actual_size;
   size_t _active_heap;
   static const size_t _alloc_memory_mask = size_t(1) << 31;
   static const size_t _memory_state_mask = _alloc_memory_mask | _aligned_memory_mask;
};

memory_manager memory_heap;

struct aligned_allocation_header {
   static constexpr std::uint32_t magic_value = 0xa1196e4dU;

   std::uint32_t magic = magic_value;
   void* base = nullptr;
   size_t size = 0;
   aligned_allocation_header* previous = nullptr;
   aligned_allocation_header* next = nullptr;
};

static aligned_allocation_header* aligned_allocations = nullptr;

static void register_aligned(aligned_allocation_header* header) {
   header->previous = nullptr;
   header->next = aligned_allocations;
   if (aligned_allocations != nullptr) {
      aligned_allocations->previous = header;
   }
   aligned_allocations = header;
}

static void unregister_aligned(aligned_allocation_header* header) {
   if (header->previous != nullptr) {
      header->previous->next = header->next;
   } else {
      aligned_allocations = header->next;
   }
   if (header->next != nullptr) {
      header->next->previous = header->previous;
   }
   header->magic = 0U;
   header->previous = nullptr;
   header->next = nullptr;
}

aligned_allocation_header* aligned_header(void* ptr) {
   if (ptr == nullptr) {
      return nullptr;
   }

   for (auto* header = aligned_allocations; header != nullptr; header = header->next) {
      auto* const result = reinterpret_cast<char*>(header) + sizeof(aligned_allocation_header);
      if (result != ptr || header->magic != aligned_allocation_header::magic_value) {
         continue;
      }

      size_t allocation_size = 0;
      if (!memory_heap.aligned_allocation_size(header->base, allocation_size)) {
         return nullptr;
      }

      const auto base = reinterpret_cast<std::uintptr_t>(header->base);
      const auto address = reinterpret_cast<std::uintptr_t>(ptr);
      if (address < base || address - base < sizeof(aligned_allocation_header) || address - base > allocation_size ||
          header->size > allocation_size - (address - base)) {
         return nullptr;
      }
      return header;
   }
   return nullptr;
}
} // namespace forge::contract::runtime

extern "C" {
void* malloc(size_t size) {
   return forge::contract::runtime::memory_heap.malloc(size);
}

void* aligned_alloc(size_t alignment, size_t size) {
   if (alignment == 0U || (alignment & (alignment - 1U)) != 0U || alignment % alignof(void*) != 0U ||
       size % alignment != 0U) {
      return nullptr;
   }
   const auto overhead = alignment - 1U + sizeof(forge::contract::runtime::aligned_allocation_header);
   if (size > std::numeric_limits<size_t>::max() - overhead) {
      return nullptr;
   }

   const auto total = size + overhead;
   auto* base = forge::contract::runtime::memory_heap.malloc(total);
   if (base == nullptr) {
      return nullptr;
   }

   const auto start =
       reinterpret_cast<std::uintptr_t>(base) + sizeof(forge::contract::runtime::aligned_allocation_header);
   const auto address = (start + alignment - 1U) & ~(static_cast<std::uintptr_t>(alignment) - 1U);
   auto* result = reinterpret_cast<void*>(address);
   auto* header = reinterpret_cast<forge::contract::runtime::aligned_allocation_header*>(
       static_cast<char*>(result) - sizeof(forge::contract::runtime::aligned_allocation_header));
   *header = {.base = base, .size = size};
   if (!forge::contract::runtime::memory_heap.mark_aligned(base)) {
      forge::contract::runtime::memory_heap.free(base);
      return nullptr;
   }
   forge::contract::runtime::register_aligned(header);
   return result;
}

void* calloc(size_t count, size_t size) {
   if (size != 0U && count > std::numeric_limits<size_t>::max() / size) {
      return nullptr;
   }
   const auto total = count * size;
   void* ptr = forge::contract::runtime::memory_heap.malloc(total);
   if (ptr != nullptr) {
      memset(ptr, 0, total);
   }
   return ptr;
}

void* realloc(void* ptr, size_t size) {
   if (auto* header = forge::contract::runtime::aligned_header(ptr)) {
      if (size == 0U) {
         auto* base = header->base;
         forge::contract::runtime::unregister_aligned(header);
         forge::contract::runtime::memory_heap.free(base);
         return nullptr;
      }

      auto* result = forge::contract::runtime::memory_heap.malloc(size);
      if (result == nullptr) {
         return nullptr;
      }
      memcpy(result, ptr, std::min(size, header->size));
      auto* base = header->base;
      forge::contract::runtime::unregister_aligned(header);
      forge::contract::runtime::memory_heap.free(base);
      return result;
   }
   return forge::contract::runtime::memory_heap.realloc(ptr, size);
}

void free(void* ptr) {
   if (auto* header = forge::contract::runtime::aligned_header(ptr)) {
      auto* base = header->base;
      forge::contract::runtime::unregister_aligned(header);
      forge::contract::runtime::memory_heap.free(base);
      return;
   }
   return forge::contract::runtime::memory_heap.free(ptr);
}
}

#undef CURRENT_MEMORY
#undef GROW_MEMORY
