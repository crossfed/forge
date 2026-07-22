module;

#include <cstddef>
#include <string_view>

export module forge.contract.hash_id;

export import forge.chain.protocol.hash_id;

export namespace forge::contract {

using chain::protocol::hash_id;

} // namespace forge::contract

export consteval forge::contract::hash_id operator""_i(const char* value, std::size_t size) {
   return forge::contract::hash_id{std::string_view{value, size}};
}
