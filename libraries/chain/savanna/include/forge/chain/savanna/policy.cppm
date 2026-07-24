module;

#include <boost/describe.hpp>

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

export module forge.chain.savanna.policy;

export import forge.chain.savanna.types;
export import forge.chain.savanna.exceptions;

import forge.raw.raw;

export namespace forge::chain::savanna {

template <typename Value> struct ordered_diff {
   std::vector<std::uint16_t> remove_indexes;
   std::vector<std::pair<std::uint16_t, Value>> insert_indexes;
};

struct finalizer_policy_diff {
   std::uint32_t generation = 0;
   std::uint64_t threshold = 0;
   ordered_diff<finalizer> finalizers;
};

class verified_finalizer_policy {
 public:
   verified_finalizer_policy(const verified_finalizer_policy&) = default;
   verified_finalizer_policy(verified_finalizer_policy&&) = default;
   verified_finalizer_policy& operator=(const verified_finalizer_policy&) = default;
   verified_finalizer_policy& operator=(verified_finalizer_policy&&) = default;

   [[nodiscard]] const finalizer_policy& get() const noexcept {
      return policy_;
   }
   [[nodiscard]] std::span<const forge::crypto::bls::proof_verified_public_key> verified_keys() const noexcept {
      return keys_;
   }

 private:
   verified_finalizer_policy(finalizer_policy policy, std::vector<forge::crypto::bls::proof_verified_public_key> keys)
       : policy_(std::move(policy)), keys_(std::move(keys)) {}

   finalizer_policy policy_;
   std::vector<forge::crypto::bls::proof_verified_public_key> keys_;

   friend verified_finalizer_policy validate(finalizer_policy, std::span<const forge::crypto::bls::signature>);
   friend verified_finalizer_policy apply(const verified_finalizer_policy&, const finalizer_policy_diff&,
                                          std::span<const forge::crypto::bls::signature>);
};

[[nodiscard]] verified_finalizer_policy validate(finalizer_policy policy,
                                                 std::span<const forge::crypto::bls::signature> proofs);
[[nodiscard]] verified_finalizer_policy apply(const verified_finalizer_policy& source,
                                              const finalizer_policy_diff& difference,
                                              std::span<const forge::crypto::bls::signature> inserted_proofs);

BOOST_DESCRIBE_STRUCT(finalizer_policy_diff, (), (generation, threshold, finalizers))

} // namespace forge::chain::savanna

export namespace forge::raw {

template <typename Stream, typename Value>
void raw_pack(Stream& stream, const forge::chain::savanna::ordered_diff<Value>& value) {
   forge::raw::pack(stream, value.remove_indexes);
   forge::raw::pack(stream, value.insert_indexes);
}

template <typename Stream, typename Value>
void raw_unpack(Stream& stream, forge::chain::savanna::ordered_diff<Value>& value) {
   forge::raw::unpack(stream, value.remove_indexes);
   forge::raw::unpack(stream, value.insert_indexes);
}

} // namespace forge::raw
