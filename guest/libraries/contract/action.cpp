module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

module forge.contract.action;

import forge.contract.intrinsics;
import forge.raw.codec;

namespace forge::contract {

void require_recipient(name account) {
   internal::require_recipient(account.value);
}

void require_auth(name account) {
   internal::require_auth(account.value);
}

void require_auth(const permission_level& permission) {
   internal::require_auth2(permission.actor.value, permission.permission.value);
}

bool has_auth(name account) {
   return internal::has_auth(account.value);
}

bool is_account(name account) {
   return internal::is_account(account.value);
}

chain::protocol::time_point publication_time() {
   return chain::protocol::time_point{
       chain::protocol::microseconds{static_cast<std::int64_t>(internal::publication_time())}};
}

checksum256 get_code_hash(name account, code_hash_result* full_result) {
   auto local_result = code_hash_result{};
   auto& result = full_result == nullptr ? local_result : *full_result;
   auto buffer = std::vector<std::uint8_t>(64U);
   constexpr auto version = std::uint32_t{0};
   auto size = internal::get_code_hash(account.value, version, reinterpret_cast<char*>(buffer.data()), buffer.size());
   if (size > buffer.size()) {
      buffer.resize(size);
      size = internal::get_code_hash(account.value, version, reinterpret_cast<char*>(buffer.data()), buffer.size());
   }
   check(size <= buffer.size(), "code hash result exceeds reported size");
   result = forge::raw::unpack_exact<code_hash_result>(
       std::span<const std::uint8_t>{buffer.data(), static_cast<std::size_t>(size)});
   check(result.struct_version.value == version, "hypervisor returned unexpected code hash struct version");
   return result.code_hash;
}

void action::send() const {
   auto serialized = forge::raw::pack(static_cast<const chain::protocol::action&>(*this));
   check(serialized.size() <= std::numeric_limits<std::uint32_t>::max(), "inline action is too large");
   internal::send_inline(reinterpret_cast<char*>(serialized.data()), serialized.size());
}

void action::send_context_free() const {
   check(authorization.empty(), "context free actions cannot have authorizations");
   auto serialized = forge::raw::pack(static_cast<const chain::protocol::action&>(*this));
   check(serialized.size() <= std::numeric_limits<std::uint32_t>::max(), "context-free action is too large");
   internal::send_context_free_inline(reinterpret_cast<char*>(serialized.data()), serialized.size());
}

} // namespace forge::contract
