#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace forge::net::p2p::detail::ipns_protobuf {

struct wire_record {
   std::optional<std::vector<std::uint8_t>> value;
   std::optional<std::vector<std::uint8_t>> signature_v1;
   std::optional<std::uint64_t> validity_type;
   std::optional<std::vector<std::uint8_t>> validity;
   std::optional<std::uint64_t> sequence;
   std::optional<std::uint64_t> ttl;
   std::optional<std::vector<std::uint8_t>> public_key;
   std::optional<std::vector<std::uint8_t>> signature_v2;
   std::optional<std::vector<std::uint8_t>> data;
   std::vector<std::uint8_t> unknown_fields;
};

[[nodiscard]] std::vector<std::uint8_t> encode(const wire_record& value);
[[nodiscard]] wire_record decode(std::span<const std::uint8_t> bytes);

} // namespace forge::net::p2p::detail::ipns_protobuf
