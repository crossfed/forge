module;

#include <cstdint>

export module forge.chain.protocol.call_data_header;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct call_data_header {
   std::uint32_t version = 0;
   std::uint64_t func_name = 0;
};

template <typename Stream> void raw_pack(Stream& stream, const call_data_header& value) {
   forge::raw::pack(stream, value.version);
   forge::raw::pack(stream, value.func_name);
}

template <typename Stream> void raw_unpack(Stream& stream, call_data_header& value) {
   forge::raw::unpack(stream, value.version);
   forge::raw::unpack(stream, value.func_name);
}

} // namespace forge::chain::protocol
