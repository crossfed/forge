module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.ratio;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct ratio {
   std::uint64_t numerator = 0;
   std::uint64_t denominator = 1;

   bool operator==(const ratio&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const ratio& value) {
   forge::raw::pack(stream, value.numerator);
   forge::raw::pack(stream, value.denominator);
}

template <typename Stream> void raw_unpack(Stream& stream, ratio& value) {
   forge::raw::unpack(stream, value.numerator);
   forge::raw::unpack(stream, value.denominator);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(ratio, (), (numerator, denominator))
} // namespace forge::chain::protocol
#endif
