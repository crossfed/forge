module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

export module forge.chain.protocol.permission_usage;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.time;

export namespace forge::chain::protocol {

struct permission_usage {
   permission_usage_id id;
   time_point last_used{};

   bool operator==(const permission_usage&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(permission_usage, (), (id, last_used))
} // namespace forge::chain::protocol
#endif
