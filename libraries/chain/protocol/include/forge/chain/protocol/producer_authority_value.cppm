module;

#include <cstdint>
#include <variant>
#include <vector>

export module forge.chain.protocol.producer_authority:value;

export import forge.chain.protocol.authority;
export import forge.chain.protocol.producer_schedule;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct block_signing_authority_v0 {
   std::uint32_t threshold = 0;
   std::vector<key_weight> keys;

   bool operator==(const block_signing_authority_v0&) const = default;
};

using block_signing_authority = std::variant<block_signing_authority_v0>;

struct producer_authority {
   account_name producer_name;
   block_signing_authority authority;

   bool operator==(const producer_authority&) const = default;

   friend constexpr bool operator<(const producer_authority& left, const producer_authority& right) {
      return left.producer_name < right.producer_name;
   }
};

struct producer_authority_schedule {
   std::uint32_t version = 0;
   std::vector<producer_authority> producers;

   bool operator==(const producer_authority_schedule&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const block_signing_authority_v0& value) {
   forge::raw::pack(stream, value.threshold);
   forge::raw::pack(stream, value.keys);
}

template <typename Stream> void raw_unpack(Stream& stream, block_signing_authority_v0& value) {
   forge::raw::unpack(stream, value.threshold);
   forge::raw::unpack(stream, value.keys);
}

template <typename Stream> void raw_pack(Stream& stream, const producer_authority& value) {
   forge::raw::pack(stream, value.producer_name);
   forge::raw::pack(stream, value.authority);
}

template <typename Stream> void raw_unpack(Stream& stream, producer_authority& value) {
   forge::raw::unpack(stream, value.producer_name);
   forge::raw::unpack(stream, value.authority);
}

template <typename Stream> void raw_pack(Stream& stream, const producer_authority_schedule& value) {
   forge::raw::pack(stream, value.version);
   forge::raw::pack(stream, value.producers);
}

template <typename Stream> void raw_unpack(Stream& stream, producer_authority_schedule& value) {
   forge::raw::unpack(stream, value.version);
   forge::raw::unpack(stream, value.producers);
}

} // namespace forge::chain::protocol
