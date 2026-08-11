module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>
#endif

#include <cstdint>
#include <string>
#include <vector>

export module forge.chain.savanna.values;

export import forge.crypto.bls.values;

#if !defined(FORGE_CONTRACT_GUEST)
export import forge.crypto.bls.serialization;
#endif

import forge.raw.codec;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.described;
import forge.variant.value;
#endif

export namespace forge::chain::savanna {

struct finalizer {
   std::string description;
   std::uint64_t weight = 0;
   forge::crypto::bls::public_key public_key;

   auto operator<=>(const finalizer&) const = default;
};

struct finalizer_policy {
   std::uint32_t generation = 0;
   std::uint64_t threshold = 0;
   std::vector<finalizer> finalizers;

   auto operator<=>(const finalizer_policy&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const finalizer& value) {
   forge::raw::pack(stream, value.description);
   forge::raw::pack(stream, value.weight);
   forge::raw::pack(stream, value.public_key);
}

template <typename Stream> void raw_unpack(Stream& stream, finalizer& value) {
   forge::raw::unpack(stream, value.description);
   forge::raw::unpack(stream, value.weight);
   forge::raw::unpack(stream, value.public_key);
}

template <typename Stream> void raw_pack(Stream& stream, const finalizer_policy& value) {
   forge::raw::pack(stream, value.generation);
   forge::raw::pack(stream, value.threshold);
   forge::raw::pack(stream, value.finalizers);
}

template <typename Stream> void raw_unpack(Stream& stream, finalizer_policy& value) {
   forge::raw::unpack(stream, value.generation);
   forge::raw::unpack(stream, value.threshold);
   forge::raw::unpack(stream, value.finalizers);
}

#if !defined(FORGE_CONTRACT_GUEST)
BOOST_DESCRIBE_STRUCT(finalizer, (), (description, weight, public_key))
BOOST_DESCRIBE_STRUCT(finalizer_policy, (), (generation, threshold, finalizers))
#endif

} // namespace forge::chain::savanna

#if !defined(FORGE_CONTRACT_GUEST)
FORGE_DECLARE_SERIALIZATION(forge::chain::savanna::finalizer)
FORGE_DECLARE_SERIALIZATION(forge::chain::savanna::finalizer_policy)
#endif
