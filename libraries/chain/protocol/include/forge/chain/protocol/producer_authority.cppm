module;

#include <cstdint>
#include <variant>
#include <vector>

export module forge.chain.protocol.producer_authority;

export import forge.chain.protocol.authority;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct producer_key {
   account_name producer_name;
   public_key block_signing_key;

   friend constexpr bool operator<(const producer_key& left, const producer_key& right) {
      return left.producer_name < right.producer_name;
   }
};

struct producer_schedule {
   std::uint32_t version = 0;
   std::vector<producer_key> producers;
};

struct block_signing_authority_v0 {
   std::uint32_t threshold = 0;
   std::vector<key_weight> keys;
};

using block_signing_authority = std::variant<block_signing_authority_v0>;

struct producer_authority {
   account_name producer_name;
   block_signing_authority authority;

   friend constexpr bool operator<(const producer_authority& left, const producer_authority& right) {
      return left.producer_name < right.producer_name;
   }
};

struct producer_authority_schedule {
   std::uint32_t version = 0;
   std::vector<producer_authority> producers;
};

template <typename Stream> void raw_pack(Stream& stream, const producer_key& value) {
   forge::raw::pack(stream, value.producer_name);
   forge::raw::pack(stream, value.block_signing_key);
}

template <typename Stream> void raw_unpack(Stream& stream, producer_key& value) {
   forge::raw::unpack(stream, value.producer_name);
   forge::raw::unpack(stream, value.block_signing_key);
}

template <typename Stream> void raw_pack(Stream& stream, const producer_schedule& value) {
   forge::raw::pack(stream, value.version);
   forge::raw::pack(stream, value.producers);
}

template <typename Stream> void raw_unpack(Stream& stream, producer_schedule& value) {
   forge::raw::unpack(stream, value.version);
   forge::raw::unpack(stream, value.producers);
}

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
