module;

#include <forge/exceptions/policy.hpp>

#include <new>
#include <span>
#include <vector>

module forge.raw.codec;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.raw.exceptions;
#endif

namespace forge::raw::detail {

datastream<std::vector<std::uint8_t>> make_input_stream(std::span<const std::uint8_t> input) {
   return datastream<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{input.begin(), input.end()}};
}

[[noreturn]] void fail_codec(const char* message) {
   FORGE_POLICY_THROW_EXCEPTION(forge::raw::exceptions::codec_error, message);
}

} // namespace forge::raw::detail
