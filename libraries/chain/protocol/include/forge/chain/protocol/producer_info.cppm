module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

export module forge.chain.protocol.producer_info;

export import forge.chain.protocol.float64;
export import forge.chain.protocol.producer_authority;
export import forge.chain.protocol.types;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct producer_info {
   account_name owner;
   float64 total_votes;
   public_key producer_key;
   bool is_active = true;
   std::string url;
   std::uint32_t unpaid_blocks = 0;
   time_point last_claim_time{};
   std::uint16_t location = 0;
   std::optional<block_signing_authority> producer_authority;

   bool operator==(const producer_info&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const producer_info& value) {
   forge::raw::pack(stream, value.owner);
   forge::raw::pack(stream, value.total_votes);
   forge::raw::pack(stream, value.producer_key);
   forge::raw::pack(stream, value.is_active);
   forge::raw::pack(stream, value.url);
   forge::raw::pack(stream, value.unpaid_blocks);
   forge::raw::pack(stream, value.last_claim_time);
   forge::raw::pack(stream, value.location);
   if (value.producer_authority) {
      forge::raw::pack(stream, *value.producer_authority);
   }
}

template <typename Stream> void raw_unpack(Stream& stream, producer_info& value) {
   forge::raw::unpack(stream, value.owner);
   forge::raw::unpack(stream, value.total_votes);
   forge::raw::unpack(stream, value.producer_key);
   forge::raw::unpack(stream, value.is_active);
   forge::raw::unpack(stream, value.url);
   forge::raw::unpack(stream, value.unpaid_blocks);
   forge::raw::unpack(stream, value.last_claim_time);
   forge::raw::unpack(stream, value.location);
   if (stream.remaining() == 0U) {
      value.producer_authority.reset();
      return;
   }
   auto authority = block_signing_authority{};
   forge::raw::unpack(stream, authority);
   value.producer_authority.emplace(std::move(authority));
}

BOOST_DESCRIBE_STRUCT(producer_info, (),
                      (owner, total_votes, producer_key, is_active, url, unpaid_blocks, last_claim_time, location,
                       producer_authority))

} // namespace forge::chain::protocol
