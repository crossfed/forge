module;

#include <forge/exceptions/policy.hpp>

#include <string>

module forge.raw.stream;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.raw.exceptions;
#endif

namespace forge::raw::detail {

[[noreturn]] void raise_stream_range(const char* operation, std::size_t length, std::int64_t overrun) {
   FORGE_POLICY_THROW_EXCEPTION(forge::raw::exceptions::range_error, std::string(operation) + " datastream of length " +
                                                                         std::to_string(length) + " over by " +
                                                                         std::to_string(overrun));
}

} // namespace forge::raw::detail
