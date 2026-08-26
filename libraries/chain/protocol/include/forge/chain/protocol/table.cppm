module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.table;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.types;

export namespace forge::chain::protocol {

struct table {
   table_id id;
   account_name code;
   name scope;
   table_name table;
   account_name payer;
   std::uint32_t count = 0;

   bool operator==(const struct table&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(table, (), (id, code, scope, table, payer, count))
} // namespace forge::chain::protocol
#endif
