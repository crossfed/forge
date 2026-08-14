module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module forge.net.p2p.dht;

import forge.crypto.digest.sha256;
import forge.multiformats.multiaddr;
import forge.multiformats.multihash;
import forge.multiformats.exceptions;
import forge.multiformats.types;
import forge.multiformats.varint;
import forge.net.p2p.exceptions;

#include "details/protobuf.hxx"

namespace forge::net::p2p {
namespace {

constexpr auto amino_provider_key_limit = std::size_t{80};

[[nodiscard]] std::vector<std::uint8_t> endpoint_bytes(const endpoint& value) {
   return forge::multiformats::multiaddr::parse(value.to_string()).to_bytes();
}

[[nodiscard]] endpoint endpoint_from_bytes(std::span<const std::uint8_t> value) {
   return parse_endpoint(forge::multiformats::multiaddr::from_bytes(value).to_string());
}

void validate_codec_options(const dht::options& opts) {
   if (opts.max_outbound_message_size == 0 || opts.max_inbound_message_size == 0 || opts.max_record_size == 0 ||
       opts.max_closer_peers == 0 || opts.max_provider_peers == 0 || opts.max_peer_endpoints == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid DHT codec options");
   }
}

[[nodiscard]] bool valid_amino_provider_key(std::span<const std::uint8_t> value) {
   if (value.empty() || value.size() > amino_provider_key_limit) {
      return false;
   }
   try {
      (void)forge::multiformats::multihash::decode(value);
      return true;
   } catch (const forge::multiformats::exceptions::invalid_format&) {
      return false;
   }
}

void validate_outbound_provider_key(const dht::message& value) {
   if (value.type != dht::message_type::add_provider && value.type != dht::message_type::get_providers) {
      return;
   }
   // Rust libp2p omits the request key from non-PUT responses. Request
   // semantics validate the key after decoding, where direction is known.
   if (value.key_value.bytes.empty()) {
      return;
   }
   if (!valid_amino_provider_key(value.key_value.bytes)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                            "Amino DHT provider key must be a multihash of at most 80 bytes");
   }
}

void validate_inbound_provider_key(const dht::message& value) {
   if (value.type != dht::message_type::add_provider && value.type != dht::message_type::get_providers) {
      return;
   }
   if (value.key_value.bytes.empty()) {
      return;
   }
   if (!valid_amino_provider_key(value.key_value.bytes)) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "Amino DHT provider key must be a multihash of at most 80 bytes");
   }
}

[[nodiscard]] dht::message_type checked_message_type(std::uint64_t value) {
   switch (static_cast<dht::message_type>(value)) {
   case dht::message_type::put_value:
   case dht::message_type::get_value:
   case dht::message_type::add_provider:
   case dht::message_type::get_providers:
   case dht::message_type::find_node:
   case dht::message_type::ping:
      return static_cast<dht::message_type>(value);
   }
   FORGE_THROW_EXCEPTION(exceptions::codec_error, "unknown DHT message type");
}

[[nodiscard]] dht::connection_type checked_connection_type(std::uint64_t value) {
   switch (static_cast<dht::connection_type>(value)) {
   case dht::connection_type::not_connected:
   case dht::connection_type::connected:
   case dht::connection_type::can_connect:
   case dht::connection_type::cannot_connect:
      return static_cast<dht::connection_type>(value);
   }
   FORGE_THROW_EXCEPTION(exceptions::codec_error, "unknown DHT peer connection type");
}

[[nodiscard]] std::vector<std::uint8_t> encode_record_payload(const dht::record& value, const dht::options& opts) {
   if (value.key_value.bytes.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT record key must not be empty");
   }
   if (value.value.size() > opts.max_record_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT record exceeds max size");
   }
   if (value.ttl.count() < 0 ||
       static_cast<std::uint64_t>(value.ttl.count()) > std::numeric_limits<std::uint32_t>::max()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT record ttl must fit uint32");
   }
   auto out = std::vector<std::uint8_t>{};
   detail::append_bytes(out, 1, value.key_value.bytes);
   detail::append_bytes(out, 2, value.value);
   if (!value.time_received.empty()) {
      detail::append_string(out, 5, value.time_received);
   }
   if (value.publisher) {
      detail::append_bytes(out, 666, value.publisher->to_bytes());
   }
   if (value.ttl.count() > 0) {
      detail::append_uint64(out, 777, static_cast<std::uint64_t>(value.ttl.count()));
   }
   return out;
}

[[nodiscard]] dht::record decode_record_payload(std::span<const std::uint8_t> bytes, const dht::options& opts) {
   auto out = dht::record{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT record key must be bytes");
         }
         out.key_value = dht::key{.bytes = in.bytes()};
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT record value must be bytes");
         }
         {
            auto value = in.bytes();
            if (value.size() > opts.max_record_size) {
               FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT record exceeds max size");
            }
            out.value = std::move(value);
         }
         break;
      case 5:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT record timeReceived must be bytes");
         }
         out.time_received = in.string();
         break;
      case 666:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT record publisher must be bytes");
         }
         {
            auto publisher = peer_id::from_bytes(in.bytes());
            out.publisher = std::move(publisher);
         }
         break;
      case 777:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT record ttl must be varint");
         }
         {
            const auto ttl = in.read_varint();
            if (ttl > std::numeric_limits<std::uint32_t>::max()) {
               FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT record ttl exceeds uint32");
            }
            out.ttl = std::chrono::seconds{static_cast<std::uint32_t>(ttl)};
         }
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (out.key_value.bytes.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT record missing key");
   }
   return out;
}

void append_peer(std::vector<std::uint8_t>& out, std::uint32_t field, const dht::peer& value,
                 const dht::options& opts) {
   if (!valid_peer_id(value.id)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT peer has invalid Peer ID");
   }
   auto encoded = std::vector<std::uint8_t>{};
   detail::append_bytes(encoded, 1, value.id.to_bytes());
   if (value.endpoints.size() > opts.max_peer_endpoints) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT peer has too many endpoints");
   }
   for (const auto& endpoint : value.endpoints) {
      detail::append_bytes(encoded, 2, endpoint_bytes(endpoint));
   }
   detail::append_uint64(encoded, 3, static_cast<std::uint16_t>(value.connection));
   if (encoded.size() > opts.max_outbound_message_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT peer record exceeds max message size");
   }
   detail::append_bytes(out, field, encoded);
}

[[nodiscard]] dht::peer decode_peer(std::span<const std::uint8_t> bytes, const dht::options& opts) {
   if (bytes.size() > opts.max_inbound_message_size) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT peer record exceeds max message size");
   }
   auto out = dht::peer{};
   auto saw_id = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT peer id must be bytes");
         }
         {
            auto id = peer_id::from_bytes(in.bytes());
            out.id = std::move(id);
         }
         saw_id = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT peer address must be bytes");
         }
         if (out.endpoints.size() >= opts.max_peer_endpoints) {
            in.skip(type);
            break;
         }
         {
            const auto address = in.bytes();
            if (address.size() > opts.max_inbound_message_size) {
               FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT peer address exceeds max message size");
            }
            auto decoded = endpoint_from_bytes(address);
            out.endpoints.push_back(std::move(decoded));
         }
         break;
      case 3:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT peer connection type must be varint");
         }
         {
            const auto connection = checked_connection_type(in.read_varint());
            out.connection = connection;
         }
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_id) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT peer missing id");
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_payload(const dht::message& value, const dht::options& opts) {
   auto out = std::vector<std::uint8_t>{};
   if (value.type != dht::message_type::put_value) {
      detail::append_uint64(out, 1, static_cast<std::uint16_t>(value.type));
   }
   if (value.cluster_level_raw != 0) {
      detail::append_uint64(out, 10, static_cast<std::uint64_t>(value.cluster_level_raw));
   }
   if (!value.key_value.bytes.empty()) {
      detail::append_bytes(out, 2, value.key_value.bytes);
   }
   if (value.record_value) {
      const auto record = encode_record_payload(*value.record_value, opts);
      detail::append_bytes(out, 3, record);
   }
   if (value.closer_peers.size() > opts.max_closer_peers) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT message has too many closer peers");
   }
   for (const auto& peer : value.closer_peers) {
      append_peer(out, 8, peer, opts);
   }
   if (value.provider_peers.size() > opts.max_provider_peers) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT message has too many provider peers");
   }
   for (const auto& peer : value.provider_peers) {
      append_peer(out, 9, peer, opts);
   }
   return out;
}

} // namespace

std::vector<std::uint8_t> dht::codec::encode(const dht::message& value) {
   return encode(value, dht::options{});
}

std::vector<std::uint8_t> dht::codec::encode(const dht::message& value, const dht::options& opts) {
   validate_codec_options(opts);
   const auto payload = encode_payload(value, opts);
   if (payload.size() > opts.max_outbound_message_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT message exceeds max size");
   }
   return detail::wrap_message(payload);
}

dht::message dht::codec::decode(std::span<const std::uint8_t> bytes) {
   return decode(bytes, dht::options{});
}

dht::message dht::codec::decode(std::span<const std::uint8_t> bytes, const dht::options& opts) {
   validate_codec_options(opts);
   const auto payload = detail::unwrap_message(bytes, opts.max_inbound_message_size);
   auto out = dht::message{.type = dht::message_type::put_value};
   auto in = detail::reader{payload};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT message type must be varint");
         }
         {
            const auto message_type = checked_message_type(in.read_varint());
            out.type = message_type;
         }
         break;
      case 10:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT cluster level must be varint");
         }
         out.cluster_level_raw = static_cast<std::int32_t>(in.read_varint());
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT key must be bytes");
         }
         {
            auto key = dht::key{.bytes = in.bytes()};
            out.key_value = std::move(key);
         }
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT record must be bytes");
         }
         {
            auto record = decode_record_payload(in.bytes(), opts);
            out.record_value = std::move(record);
         }
         break;
      case 8:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT closer peer must be bytes");
         }
         if (out.closer_peers.size() >= opts.max_closer_peers) {
            in.skip(type);
            break;
         }
         {
            auto peer = decode_peer(in.bytes(), opts);
            out.closer_peers.push_back(std::move(peer));
         }
         break;
      case 9:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "DHT provider peer must be bytes");
         }
         if (out.provider_peers.size() >= opts.max_provider_peers) {
            in.skip(type);
            break;
         }
         {
            auto peer = decode_peer(in.bytes(), opts);
            out.provider_peers.push_back(std::move(peer));
         }
         break;
      default:
         in.skip(type);
         break;
      }
   }
   return out;
}

std::vector<std::uint8_t> dht::codec::encode(const dht::message& value, const dht::profile& profile_value) {
   validate(profile_value);
   if (profile_value.kind == dht::profile_kind::amino_v1) {
      validate_outbound_provider_key(value);
   }
   return encode(value, profile_value.limits);
}

dht::message dht::codec::decode(std::span<const std::uint8_t> bytes, const dht::profile& profile_value) {
   validate(profile_value);
   auto result = decode(bytes, profile_value.limits);
   if (profile_value.kind == dht::profile_kind::amino_v1) {
      validate_inbound_provider_key(result);
   }
   return result;
}

dht::key make_dht_key(std::span<const std::uint8_t> value) {
   return dht::key{.bytes = std::vector<std::uint8_t>{value.begin(), value.end()}};
}

dht::key make_dht_key(const peer_id& peer) {
   return dht::key{.bytes = peer.to_bytes()};
}

dht::distance distance_between(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right) {
   const auto left_digest = forge::crypto::digest::sha256::hash(left);
   const auto right_digest = forge::crypto::digest::sha256::hash(right);
   const auto left_hash = left_digest.to_uint8_span();
   const auto right_hash = right_digest.to_uint8_span();
   auto out = dht::distance{};
   for (auto i = std::size_t{}; i < out.bytes.size(); ++i) {
      out.bytes[i] = static_cast<std::uint8_t>(left_hash[i] ^ right_hash[i]);
   }
   return out;
}

} // namespace forge::net::p2p
