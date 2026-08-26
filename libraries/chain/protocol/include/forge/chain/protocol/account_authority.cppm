module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <vector>

export module forge.chain.protocol.account_authority;

export import forge.chain.protocol.account;
export import forge.chain.protocol.permission;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct account_authority : account {
   std::vector<permission> permissions;

   bool operator==(const account_authority&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const account_authority& value) {
   forge::raw::pack(stream, static_cast<const account&>(value));
   forge::raw::pack(stream, value.permissions);
}

template <typename Stream> void raw_unpack(Stream& stream, account_authority& value) {
   forge::raw::unpack(stream, static_cast<account&>(value));
   forge::raw::unpack(stream, value.permissions);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(account_authority, (account), (permissions))
} // namespace forge::chain::protocol
#endif
