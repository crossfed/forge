module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

export module forge.chain.protocol.full_permission;

export import forge.chain.protocol.permission;
export import forge.chain.protocol.permission_usage;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct full_permission : permission {
   permission_usage usage;

   bool operator==(const full_permission&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const full_permission& value) {
   forge::raw::pack(stream, static_cast<const permission&>(value));
   forge::raw::pack(stream, value.usage);
}

template <typename Stream> void raw_unpack(Stream& stream, full_permission& value) {
   forge::raw::unpack(stream, static_cast<permission&>(value));
   forge::raw::unpack(stream, value.usage);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(full_permission, (permission), (usage))
} // namespace forge::chain::protocol
#endif
