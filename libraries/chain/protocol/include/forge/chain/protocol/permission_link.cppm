module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

export module forge.chain.protocol.permission_link;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.types;

export namespace forge::chain::protocol {

struct permission_link {
   permission_link_id id;
   account_name account;
   account_name code;
   action_name message_type;
   permission_name required_permission;

   bool operator==(const permission_link&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(permission_link, (), (id, account, code, message_type, required_permission))
} // namespace forge::chain::protocol
#endif
