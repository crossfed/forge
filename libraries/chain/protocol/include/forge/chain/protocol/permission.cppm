module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

export module forge.chain.protocol.permission;

export import forge.chain.protocol.authority;
export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.time;
export import forge.chain.protocol.types;

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

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(permission, (), (id, usage_id, parent, owner, name, last_updated, auth))
} // namespace forge::chain::protocol
#endif
