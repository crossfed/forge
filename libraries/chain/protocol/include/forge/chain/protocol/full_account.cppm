module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <vector>

export module forge.chain.protocol.full_account;

export import forge.chain.protocol.account;
export import forge.chain.protocol.account_metadata;
export import forge.chain.protocol.account_resources;
export import forge.chain.protocol.full_permission;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct full_account : account {
   account_metadata metadata;
   std::vector<full_permission> permissions;
   account_resources resources;

   bool operator==(const full_account&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const full_account& value) {
   forge::raw::pack(stream, static_cast<const account&>(value));
   forge::raw::pack(stream, value.metadata);
   forge::raw::pack(stream, value.permissions);
   forge::raw::pack(stream, value.resources);
}

template <typename Stream> void raw_unpack(Stream& stream, full_account& value) {
   forge::raw::unpack(stream, static_cast<account&>(value));
   forge::raw::unpack(stream, value.metadata);
   forge::raw::unpack(stream, value.permissions);
   forge::raw::unpack(stream, value.resources);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(full_account, (account), (metadata, permissions, resources))
} // namespace forge::chain::protocol
#endif
