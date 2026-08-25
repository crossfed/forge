module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.account;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.types;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct account {
   account_id id;
   account_name name;
   block_timestamp creation_date;
   digest abi_hash;
   std::uint64_t abi_size = 0;

   bool operator==(const account&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const account& value) {
   forge::raw::pack(stream, value.id);
   forge::raw::pack(stream, value.name);
   forge::raw::pack(stream, value.creation_date);
   forge::raw::pack(stream, value.abi_hash);
   forge::raw::pack(stream, value.abi_size);
}

template <typename Stream> void raw_unpack(Stream& stream, account& value) {
   forge::raw::unpack(stream, value.id);
   forge::raw::unpack(stream, value.name);
   forge::raw::unpack(stream, value.creation_date);
   forge::raw::unpack(stream, value.abi_hash);
   forge::raw::unpack(stream, value.abi_size);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(account, (), (id, name, creation_date, abi_hash, abi_size))
} // namespace forge::chain::protocol
#endif
