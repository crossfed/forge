module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.chain.protocol.exceptions;

export import forge.exceptions;

export namespace forge::chain::protocol::exceptions {

enum class code : std::uint16_t {
   unordered_value = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.chain.protocol")

using unordered_value = forge::exceptions::coded_exception<code, code::unordered_value>;

} // namespace forge::chain::protocol::exceptions
