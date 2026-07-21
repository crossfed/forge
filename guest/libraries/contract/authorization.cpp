module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstdint>

module forge.contract.authorization;

namespace forge::contract {

bool check_transaction_authorization(const char* transaction, std::uint32_t transaction_size, const char* public_keys,
                                     std::uint32_t public_keys_size, const char* permissions,
                                     std::uint32_t permissions_size) {
   return internal::check_transaction_authorization(transaction, transaction_size, public_keys, public_keys_size,
                                                    permissions, permissions_size) > 0;
}

bool check_permission_authorization(chain::protocol::name account, chain::protocol::name permission,
                                    const char* public_keys, std::uint32_t public_keys_size, const char* permissions,
                                    std::uint32_t permissions_size, chain::protocol::microseconds delay) {
   return internal::check_permission_authorization(account.value, permission.value, public_keys, public_keys_size,
                                                   permissions, permissions_size,
                                                   static_cast<std::uint64_t>(delay.count())) > 0;
}

chain::protocol::time_point get_permission_last_used(chain::protocol::name account, chain::protocol::name permission) {
   return chain::protocol::time_point{
       chain::protocol::microseconds{internal::get_permission_last_used(account.value, permission.value)}};
}

chain::protocol::time_point get_account_creation_time(chain::protocol::name account) {
   return chain::protocol::time_point{
       chain::protocol::microseconds{internal::get_account_creation_time(account.value)}};
}

} // namespace forge::contract
