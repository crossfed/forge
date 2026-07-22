module;

#include <cstdint>
#include <vector>

export module forge.chain.protocol.producer_schedule:value;

export import forge.chain.protocol.types;

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

} // namespace forge::chain::protocol
