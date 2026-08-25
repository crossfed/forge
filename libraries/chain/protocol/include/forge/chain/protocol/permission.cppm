module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

export module forge.chain.protocol.permission;

export import forge.chain.protocol.authority;
export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.time;
export import forge.chain.protocol.types;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct permission {
   permission_id id;
   permission_usage_id usage_id;
   permission_id parent;
   account_name owner;
   permission_name name;
   time_point last_updated{};
   authority auth;

   bool operator==(const permission&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const permission& value) {
   forge::raw::pack(stream, value.id);
   forge::raw::pack(stream, value.usage_id);
   forge::raw::pack(stream, value.parent);
   forge::raw::pack(stream, value.owner);
   forge::raw::pack(stream, value.name);
   forge::raw::pack(stream, value.last_updated);
   forge::raw::pack(stream, value.auth);
}

template <typename Stream> void raw_unpack(Stream& stream, permission& value) {
   forge::raw::unpack(stream, value.id);
   forge::raw::unpack(stream, value.usage_id);
   forge::raw::unpack(stream, value.parent);
   forge::raw::unpack(stream, value.owner);
   forge::raw::unpack(stream, value.name);
   forge::raw::unpack(stream, value.last_updated);
   forge::raw::unpack(stream, value.auth);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(permission, (), (id, usage_id, parent, owner, name, last_updated, auth))
} // namespace forge::chain::protocol
#endif
