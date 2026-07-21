module;

#include <cstdint>

#if !defined(FORGE_CONTRACT_GUEST)
#include <forge/exceptions/macros.hpp>
#endif

export module forge.codec.hex.exceptions;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.exceptions;
#endif

export namespace forge::codec::hex::exceptions {

enum class code : std::uint16_t {
   invalid_input = 1,
   insufficient_output = 2,
};

#if defined(FORGE_CONTRACT_GUEST)
struct invalid_input final {};
struct insufficient_output final {};
#else
FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.codec.hex")
using invalid_input = forge::exceptions::coded_exception<code, code::invalid_input>;
using insufficient_output = forge::exceptions::coded_exception<code, code::insufficient_output>;
#endif

} // namespace forge::codec::hex::exceptions
