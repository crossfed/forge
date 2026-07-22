module;

#include <cstdint>

export module forge.chain.protocol.kv_parameters;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct kv_parameters {
   std::uint32_t max_key_size = 0;
   std::uint32_t max_value_size = 0;
   std::uint32_t max_iterators = 0;
};

template <typename Stream> void raw_pack(Stream& stream, const kv_parameters& value) {
   forge::raw::pack(stream, value.max_key_size);
   forge::raw::pack(stream, value.max_value_size);
   forge::raw::pack(stream, value.max_iterators);
}

template <typename Stream> void raw_unpack(Stream& stream, kv_parameters& value) {
   forge::raw::unpack(stream, value.max_key_size);
   forge::raw::unpack(stream, value.max_value_size);
   forge::raw::unpack(stream, value.max_iterators);
}

} // namespace forge::chain::protocol
