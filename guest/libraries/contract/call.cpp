module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstddef>
#include <cstdint>

module forge.contract.call;

namespace forge::contract {

std::int64_t call(chain::protocol::name receiver, std::uint64_t flags, const char* data, std::size_t size) {
   return internal::call(receiver.value, flags, data, size);
}

std::uint32_t get_call_return_value(void* data, std::uint32_t size) {
   return internal::get_call_return_value(data, size);
}

std::uint32_t get_call_data(void* data, std::uint32_t size) {
   return internal::get_call_data(data, size);
}

void set_call_return_value(void* data, std::uint32_t size) {
   internal::set_call_return_value(data, size);
}

} // namespace forge::contract
