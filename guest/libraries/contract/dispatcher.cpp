module;

#include <cstddef>
#include <cstdint>
#include <array>

module forge.contract.dispatcher;

namespace forge::contract {

void dispatch(chain::protocol::name receiver, chain::protocol::name code, std::uint64_t action,
              const dispatch_entry* entries, std::size_t size, const notification_entry* notifications,
              std::size_t notification_size) {
   if (code == receiver) {
      for (auto index = std::size_t{0}; index < size; ++index) {
         if (entries[index].name == action) {
            entries[index].invoke(receiver, code);
            return;
         }
      }
      return;
   }

   const notification_entry* wildcard = nullptr;
   for (auto index = std::size_t{0}; index < notification_size; ++index) {
      const auto& entry = notifications[index];
      if (entry.action != action) {
         continue;
      }
      if (entry.code == code.value) {
         entry.invoke(receiver, code);
         return;
      }
      if (entry.code == 0U) {
         wildcard = &entry;
      }
   }
   if (wildcard != nullptr) {
      wildcard->invoke(receiver, code);
   }
}

void dispatch_call(chain::protocol::name sender, chain::protocol::name receiver, const call_entry* entries,
                   std::size_t size) {
   const auto data_size = get_call_data(nullptr, 0U);
   check(data_size >= sizeof(std::uint32_t) + sizeof(std::uint64_t), "sync call data header is incomplete");
   auto header_bytes = std::array<std::uint8_t, sizeof(std::uint32_t) + sizeof(std::uint64_t)>{};
   check(get_call_data(header_bytes.data(), header_bytes.size()) == header_bytes.size(),
         "failed to read sync call data header");
   auto header = forge::raw::unpack_exact<call_data_header>(header_bytes);
   check(header.version == 0U, "unsupported sync call data version");
   for (auto index = std::size_t{0}; index < size; ++index) {
      if (entries[index].id == header.func_name) {
         entries[index].invoke(sender, receiver);
         return;
      }
   }
   check(false, "contract does not expose requested sync call");
}

} // namespace forge::contract
