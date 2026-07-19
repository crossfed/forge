module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstdint>
#include <string>
#include <vector>

export module forge.contract.instant_finality;

export import forge.contract.crypto_bls_ext;

import forge.contract.datastream;
import forge.contract.intrinsics;

export namespace forge::contract {

struct finalizer_authority {
   std::string description;
   std::uint64_t weight = 0;
   std::vector<char> public_key;
};

struct finalizer_policy {
   std::uint64_t threshold = 0;
   std::vector<finalizer_authority> finalizers;
};

template <typename Stream> void raw_pack(Stream& stream, const finalizer_authority& value) {
   ::forge::raw::pack(stream, value.description);
   ::forge::raw::pack(stream, value.weight);
   ::forge::raw::pack(stream, value.public_key);
}

template <typename Stream> void raw_unpack(Stream& stream, finalizer_authority& value) {
   ::forge::raw::unpack(stream, value.description);
   ::forge::raw::unpack(stream, value.weight);
   ::forge::raw::unpack(stream, value.public_key);
}

template <typename Stream> void raw_pack(Stream& stream, const finalizer_policy& value) {
   ::forge::raw::pack(stream, value.threshold);
   ::forge::raw::pack(stream, value.finalizers);
}

template <typename Stream> void raw_unpack(Stream& stream, finalizer_policy& value) {
   ::forge::raw::unpack(stream, value.threshold);
   ::forge::raw::unpack(stream, value.finalizers);
}

inline void set_finalizers(const finalizer_policy& policy) {
   for (const auto& authority : policy.finalizers) {
      check(authority.public_key.size() == sizeof(forge::crypto::bls::g1), "public key has a wrong size");
   }
   const auto bytes = ::forge::raw::pack(policy);
   ::forge::contract::internal::set_finalizers(0U, reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace forge::contract
