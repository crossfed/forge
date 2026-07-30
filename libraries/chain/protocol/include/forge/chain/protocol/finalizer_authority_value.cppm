module;

#include <cstdint>
#include <string>
#include <vector>

export module forge.chain.protocol.finalizer_authority:value;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct finalizer_authority {
   std::string description;
   std::uint64_t weight = 0;
   std::vector<char> public_key;

   bool operator==(const finalizer_authority&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const finalizer_authority& value) {
   forge::raw::pack(stream, value.description);
   forge::raw::pack(stream, value.weight);
   forge::raw::pack(stream, value.public_key);
}

template <typename Stream> void raw_unpack(Stream& stream, finalizer_authority& value) {
   forge::raw::unpack(stream, value.description);
   forge::raw::unpack(stream, value.weight);
   forge::raw::unpack(stream, value.public_key);
}

} // namespace forge::chain::protocol
