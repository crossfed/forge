module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.table;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.types;
import forge.raw.codec;

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

template <typename Stream> void raw_pack(Stream& stream, const table& value) {
   forge::raw::pack(stream, value.id);
   forge::raw::pack(stream, value.code);
   forge::raw::pack(stream, value.scope);
   forge::raw::pack(stream, value.table);
   forge::raw::pack(stream, value.payer);
   forge::raw::pack(stream, value.count);
}

template <typename Stream> void raw_unpack(Stream& stream, table& value) {
   forge::raw::unpack(stream, value.id);
   forge::raw::unpack(stream, value.code);
   forge::raw::unpack(stream, value.scope);
   forge::raw::unpack(stream, value.table);
   forge::raw::unpack(stream, value.payer);
   forge::raw::unpack(stream, value.count);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(table, (), (id, code, scope, table, payer, count))
} // namespace forge::chain::protocol
#endif
