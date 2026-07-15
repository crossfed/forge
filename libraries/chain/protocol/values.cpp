module;

#include <forge/exceptions/policy.hpp>

#if !defined(FORGE_CONTRACT_GUEST)
#include <stdexcept>
#endif

module forge.chain.protocol.values;

namespace forge::chain::protocol::detail {

[[noreturn]] void fail_value(const char* message) {
   FORGE_POLICY_THROW_STANDARD(std::invalid_argument, message);
}

} // namespace forge::chain::protocol::detail
