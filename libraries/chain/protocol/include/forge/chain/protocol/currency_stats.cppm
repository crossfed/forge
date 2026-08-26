module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

export module forge.chain.protocol.currency_stats;

export import forge.chain.protocol.types;

export namespace forge::chain::protocol {

struct currency_stats {
   asset supply;
   asset max_supply;
   account_name issuer;

   bool operator==(const currency_stats&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(currency_stats, (), (supply, max_supply, issuer))
} // namespace forge::chain::protocol
#endif
