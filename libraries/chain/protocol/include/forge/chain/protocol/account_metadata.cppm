module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.account_metadata;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.time;
export import forge.chain.protocol.types;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct account_metadata {
   metadata_id id;
   account_name name;
   std::uint64_t recv_sequence = 0;
   std::uint64_t auth_sequence = 0;
   std::uint64_t code_sequence = 0;
   std::uint64_t abi_sequence = 0;
   digest code_hash;
   time_point last_code_update{};
   std::uint32_t flags = 0;
   std::uint8_t vm_type = 0;
   std::uint8_t vm_version = 0;

   bool operator==(const account_metadata&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const account_metadata& value) {
   forge::raw::pack(stream, value.id);
   forge::raw::pack(stream, value.name);
   forge::raw::pack(stream, value.recv_sequence);
   forge::raw::pack(stream, value.auth_sequence);
   forge::raw::pack(stream, value.code_sequence);
   forge::raw::pack(stream, value.abi_sequence);
   forge::raw::pack(stream, value.code_hash);
   forge::raw::pack(stream, value.last_code_update);
   forge::raw::pack(stream, value.flags);
   forge::raw::pack(stream, value.vm_type);
   forge::raw::pack(stream, value.vm_version);
}

template <typename Stream> void raw_unpack(Stream& stream, account_metadata& value) {
   forge::raw::unpack(stream, value.id);
   forge::raw::unpack(stream, value.name);
   forge::raw::unpack(stream, value.recv_sequence);
   forge::raw::unpack(stream, value.auth_sequence);
   forge::raw::unpack(stream, value.code_sequence);
   forge::raw::unpack(stream, value.abi_sequence);
   forge::raw::unpack(stream, value.code_hash);
   forge::raw::unpack(stream, value.last_code_update);
   forge::raw::unpack(stream, value.flags);
   forge::raw::unpack(stream, value.vm_type);
   forge::raw::unpack(stream, value.vm_version);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(account_metadata, (),
                      (id, name, recv_sequence, auth_sequence, code_sequence, abi_sequence, code_hash,
                       last_code_update, flags, vm_type, vm_version))
} // namespace forge::chain::protocol
#endif
