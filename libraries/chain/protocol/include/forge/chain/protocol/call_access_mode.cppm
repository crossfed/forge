module;

#include <cstdint>

export module forge.chain.protocol.call_access_mode;

export namespace forge::chain::protocol {

enum class call_access_mode : std::uint8_t {
   read_write = 0,
   read_only = 1,
};

} // namespace forge::chain::protocol
