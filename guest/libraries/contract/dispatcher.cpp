module;

#include <cstddef>
#include <cstdint>

module forge.contract.dispatcher;

namespace forge::contract {

void dispatch(chain::protocol::name receiver, chain::protocol::name code, std::uint64_t action,
              const dispatch_entry* entries, std::size_t size) {
   if (code != receiver) {
      return;
   }
   for (auto index = std::size_t{0}; index < size; ++index) {
      if (entries[index].name == action) {
         entries[index].invoke(receiver, code);
         return;
      }
   }
}

} // namespace forge::contract
