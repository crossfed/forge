module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

export module forge.vm.wasm.interpret.execution_interface;

export import forge.vm.wasm.interpret.wasm_stack;

export namespace forge::vm::wasm::interpret {

// interface used for the host function system to use
// clients can create their own interface to overlay their own implementations
struct execution_interface {
   inline execution_interface(char* memory, operand_stack* os)
       : execution_interface(memory, memory == nullptr ? 0U : static_cast<std::size_t>(max_useable_memory), os) {}

   inline execution_interface(char* memory, std::size_t memory_size, operand_stack* os)
       : memory(memory), memory_size(memory_size), os(os) {}
   inline void* get_memory() const {
      return memory;
   }
   inline void trim_operands(std::size_t amt) {
      os->trim(amt);
   }

   template <typename T> inline void push_operand(T&& op) {
      os->push(std::forward<T>(op));
   }
   inline auto pop_operand() {
      return os->pop();
   }
   inline const auto& operand_from_back(std::size_t index) const {
      return os->get_back(index);
   }

   template <typename T> inline void* validate_pointer(wasm_ptr_t ptr, wasm_size_t len) const {
      detail::check<exceptions::memory>((memory != nullptr), "linear memory is not available");
      const auto size = byte_size<T>(len);
      detail::check<exceptions::memory>((ptr <= memory_size && size <= memory_size - ptr),
                                        "access exceeds linear memory");
      return memory + ptr;
   }

   template <typename T> inline void validate_pointer(const void* ptr, wasm_size_t len) const {
      const auto size = byte_size<T>(len);
      detail::check<exceptions::memory>((memory != nullptr && ptr != nullptr), "linear memory is not available");
      const auto begin = reinterpret_cast<std::uintptr_t>(memory);
      const auto address = reinterpret_cast<std::uintptr_t>(ptr);
      detail::check<exceptions::memory>((address >= begin && address - begin <= memory_size),
                                        "pointer is outside linear memory");
      const auto offset = address - begin;
      detail::check<exceptions::memory>((size <= memory_size - offset), "access exceeds linear memory");
   }

   inline void* validate_null_terminated_pointer(wasm_ptr_t ptr) const {
      detail::check<exceptions::memory>((memory != nullptr), "linear memory is not available");
      detail::check<exceptions::memory>((ptr < memory_size), "pointer is outside linear memory");
      auto* result = memory + ptr;
      validate_null_terminated_pointer(result);
      return result;
   }

   inline void validate_null_terminated_pointer(const void* ptr) const {
      detail::check<exceptions::memory>((memory != nullptr && ptr != nullptr), "linear memory is not available");
      const auto begin = reinterpret_cast<std::uintptr_t>(memory);
      const auto address = reinterpret_cast<std::uintptr_t>(ptr);
      detail::check<exceptions::memory>((address >= begin && address - begin < memory_size),
                                        "pointer is outside linear memory");
      const auto remaining = memory_size - (address - begin);
      detail::check<exceptions::memory>((std::memchr(ptr, '\0', remaining) != nullptr),
                                        "string is not terminated within linear memory");
   }

 private:
   template <typename T> static inline std::size_t byte_size(wasm_size_t len) {
      detail::check<exceptions::interpreter>((len <= std::numeric_limits<std::size_t>::max() / sizeof(T)),
                                             "length will overflow");
      return static_cast<std::size_t>(len) * sizeof(T);
   }

 public:
   char* memory;
   std::size_t memory_size;
   operand_stack* os;
};
} // namespace forge::vm::wasm::interpret
