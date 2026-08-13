#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace forge::net::p2p::detail::ipns_cbor {

struct document {
   std::vector<std::uint8_t> value;
   std::vector<std::uint8_t> validity;
   std::int64_t validity_type = 0;
   std::int64_t sequence = 0;
   std::int64_t ttl = 0;
   ipns::metadata metadata_values;
};

[[nodiscard]] std::vector<std::uint8_t> encode(const document& value);
[[nodiscard]] document decode(std::span<const std::uint8_t> bytes);

} // namespace forge::net::p2p::detail::ipns_cbor
