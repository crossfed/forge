module;

#include <forge/exceptions/policy.hpp>

module forge.raw.codec;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.raw.exceptions;
#endif

namespace forge::raw::detail {

[[noreturn]] void fail_codec(const char* message) {
   FORGE_POLICY_THROW_EXCEPTION(forge::raw::exceptions::codec_error, message);
}

} // namespace forge::raw::detail
