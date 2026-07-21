module;

#include <cstdint>
#include <vector>

export module forge.contract.authorization;

export import forge.chain.protocol.action;
export import forge.chain.protocol.time;

import forge.contract.datastream;
import forge.contract.intrinsics;

export namespace forge::contract {

[[nodiscard]] bool check_transaction_authorization(const char* transaction, std::uint32_t transaction_size,
                                                   const char* public_keys, std::uint32_t public_keys_size,
                                                   const char* permissions, std::uint32_t permissions_size);

template <typename PublicKeys, typename Permissions>
[[nodiscard]] bool check_transaction_authorization(const std::vector<std::uint8_t>& transaction,
                                                   const PublicKeys& public_keys, const Permissions& permissions) {
   const auto packed_keys = ::forge::raw::pack(public_keys);
   const auto packed_permissions = ::forge::raw::pack(permissions);
   return check_transaction_authorization(reinterpret_cast<const char*>(transaction.data()), transaction.size(),
                                          reinterpret_cast<const char*>(packed_keys.data()), packed_keys.size(),
                                          reinterpret_cast<const char*>(packed_permissions.data()),
                                          packed_permissions.size());
}

[[nodiscard]] bool
check_permission_authorization(chain::protocol::name account, chain::protocol::name permission, const char* public_keys,
                               std::uint32_t public_keys_size, const char* permissions, std::uint32_t permissions_size,
                               chain::protocol::microseconds delay = chain::protocol::microseconds{});

template <typename PublicKeys, typename Permissions>
[[nodiscard]] bool
check_permission_authorization(chain::protocol::name account, chain::protocol::name permission,
                               const PublicKeys& public_keys, const Permissions& permissions,
                               chain::protocol::microseconds delay = chain::protocol::microseconds{}) {
   const auto packed_keys = ::forge::raw::pack(public_keys);
   const auto packed_permissions = ::forge::raw::pack(permissions);
   return check_permission_authorization(account, permission, reinterpret_cast<const char*>(packed_keys.data()),
                                         packed_keys.size(), reinterpret_cast<const char*>(packed_permissions.data()),
                                         packed_permissions.size(), delay);
}

[[nodiscard]] chain::protocol::time_point get_permission_last_used(chain::protocol::name account,
                                                                   chain::protocol::name permission);
[[nodiscard]] chain::protocol::time_point get_account_creation_time(chain::protocol::name account);

} // namespace forge::contract
