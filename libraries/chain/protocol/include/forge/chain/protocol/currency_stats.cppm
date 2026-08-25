module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

export module forge.chain.protocol.currency_stats;

export import forge.chain.protocol.types;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct currency_stats {
   asset supply;
   asset max_supply;
   account_name issuer;

   bool operator==(const currency_stats&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const currency_stats& value) {
   forge::raw::pack(stream, value.supply);
   forge::raw::pack(stream, value.max_supply);
   forge::raw::pack(stream, value.issuer);
}

template <typename Stream> void raw_unpack(Stream& stream, currency_stats& value) {
   forge::raw::unpack(stream, value.supply);
   forge::raw::unpack(stream, value.max_supply);
   forge::raw::unpack(stream, value.issuer);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(currency_stats, (), (supply, max_supply, issuer))
} // namespace forge::chain::protocol
#endif
