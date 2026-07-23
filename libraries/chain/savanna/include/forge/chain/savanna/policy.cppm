module;

#include <boost/describe.hpp>

#include <cstdint>
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

void validate(const finalizer_policy& policy);
[[nodiscard]] finalizer_policy apply(const finalizer_policy& source, const finalizer_policy_diff& difference);

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
