module;

#include <cstdint>

#if !defined(FORGE_CONTRACT_GUEST)
#include <forge/exceptions/macros.hpp>
#endif

export module forge.codec.base58.exceptions;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.exceptions;
#endif

export namespace forge::codec::base58::exceptions {

enum class code : std::uint16_t {
   invalid_input = 1,
};

#if defined(FORGE_CONTRACT_GUEST)
struct invalid_input final {};
#else
FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.codec.base58")
using invalid_input = forge::exceptions::coded_exception<code, code::invalid_input>;
#endif

} // namespace forge::codec::base58::exceptions
