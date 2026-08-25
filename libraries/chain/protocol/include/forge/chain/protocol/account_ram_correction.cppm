module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.account_ram_correction;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.types;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct account_ram_correction {
   account_ram_correction_id id;
   account_name name;
   std::uint64_t ram_correction = 0;

   bool operator==(const account_ram_correction&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const account_ram_correction& value) {
   forge::raw::pack(stream, value.id);
   forge::raw::pack(stream, value.name);
   forge::raw::pack(stream, value.ram_correction);
}

template <typename Stream> void raw_unpack(Stream& stream, account_ram_correction& value) {
   forge::raw::unpack(stream, value.id);
   forge::raw::unpack(stream, value.name);
   forge::raw::unpack(stream, value.ram_correction);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(account_ram_correction, (), (id, name, ram_correction))
} // namespace forge::chain::protocol
#endif
