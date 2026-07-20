module;

#include <cstdint>
#include <vector>

export module forge.chain.protocol.finalizer_policy;

export import forge.chain.protocol.finalizer_authority;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct finalizer_policy {
   std::uint64_t threshold = 0;
   std::vector<finalizer_authority> finalizers;
};

template <typename Stream> void raw_pack(Stream& stream, const finalizer_policy& value) {
   forge::raw::pack(stream, value.threshold);
   forge::raw::pack(stream, value.finalizers);
}

template <typename Stream> void raw_unpack(Stream& stream, finalizer_policy& value) {
   forge::raw::unpack(stream, value.threshold);
   forge::raw::unpack(stream, value.finalizers);
}

} // namespace forge::chain::protocol
