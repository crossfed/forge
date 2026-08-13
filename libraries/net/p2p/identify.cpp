module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.net.p2p.identify;

import forge.multiformats.exceptions;
import forge.multiformats.types;
import forge.multiformats.varint;
import forge.multiformats.multicodec;
import forge.multiformats.multihash;
import forge.multiformats.multibase;
import forge.multiformats.multiaddr;
import forge.net.p2p.exceptions;

namespace forge::net::p2p::identify {
namespace {

enum class wire_type : std::uint8_t {
   varint = 0,
   length_delimited = 2,
};

void append_varint(std::vector<std::uint8_t>& out, std::uint64_t value) {
   auto encoded = forge::multiformats::varint_encode(value);
   out.insert(out.end(), encoded.begin(), encoded.end());
}

void append_key(std::vector<std::uint8_t>& out, std::uint32_t field, wire_type type) {
   append_varint(out, (static_cast<std::uint64_t>(field) << 3U) | static_cast<std::uint8_t>(type));
}

void append_bytes(std::vector<std::uint8_t>& out, std::uint32_t field, std::span<const std::uint8_t> value) {
   append_key(out, field, wire_type::length_delimited);
   append_varint(out, value.size());
   out.insert(out.end(), value.begin(), value.end());
}

void append_string(std::vector<std::uint8_t>& out, std::uint32_t field, std::string_view value) {
   auto bytes = std::vector<std::uint8_t>{};
   bytes.reserve(value.size());
   for (const auto ch : value) {
      bytes.push_back(static_cast<std::uint8_t>(ch));
   }
   append_bytes(out, field, bytes);
}

[[nodiscard]] std::vector<std::uint8_t> endpoint_bytes(const endpoint& value) {
   return forge::multiformats::multiaddr::parse(value.to_string()).to_bytes();
}

[[nodiscard]] std::optional<endpoint> supported_endpoint(std::span<const std::uint8_t> value) {
   const auto address = forge::multiformats::multiaddr::from_bytes(value);
   try {
      return parse_endpoint(address.to_string());
   } catch (const forge::exceptions::base&) {
      return std::nullopt;
   }
}

class reader {
 public:
   explicit reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

   [[nodiscard]] bool done() const noexcept {
      return offset_ == bytes_.size();
   }

   [[nodiscard]] std::pair<std::uint32_t, wire_type> key() {
      const auto decoded = read_varint();
      const auto type = static_cast<wire_type>(decoded & 0x07U);
      if (type != wire_type::varint && type != wire_type::length_delimited) {
         FORGE_THROW_EXCEPTION(exceptions::codec_error, "unsupported Identify protobuf wire type");
      }
      return {static_cast<std::uint32_t>(decoded >> 3U), type};
   }

   [[nodiscard]] std::uint64_t read_varint() {
      try {
         const auto decoded = forge::multiformats::varint_decode(bytes_.subspan(offset_));
         offset_ += decoded.size;
         return decoded.value;
      } catch (const forge::multiformats::exceptions::invalid_format& error) {
         FORGE_THROW_EXCEPTION(exceptions::codec_error, error.what());
      }
   }

   [[nodiscard]] std::vector<std::uint8_t> bytes(std::size_t limit) {
      const auto size = read_varint();
      if (size > bytes_.size() - offset_) {
         FORGE_THROW_EXCEPTION(exceptions::codec_error, "truncated Identify protobuf bytes field");
      }
      if (size > limit) {
         FORGE_THROW_EXCEPTION(exceptions::codec_error, "Identify protobuf field exceeds max size");
      }
      auto out = std::vector<std::uint8_t>{bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                           bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size)};
      offset_ += static_cast<std::size_t>(size);
      return out;
   }

   [[nodiscard]] std::string string(std::size_t limit) {
      const auto value = bytes(limit);
      return {value.begin(), value.end()};
   }

   void skip(wire_type type) {
      if (type == wire_type::varint) {
         (void)read_varint();
         return;
      }
      (void)bytes(bytes_.size());
   }

 private:
   std::span<const std::uint8_t> bytes_;
   std::size_t offset_ = 0;
};

} // namespace

forge::multiformats::bytes encode(const document& value) {
   auto out = forge::multiformats::bytes{};
   if (!value.public_key.empty()) {
      append_bytes(out, 1, value.public_key);
   }
   for (const auto& endpoint : value.listen_endpoints) {
      const auto bytes = endpoint_bytes(endpoint);
      append_bytes(out, 2, bytes);
   }
   for (const auto& protocol : value.protocols) {
      append_string(out, 3, protocol.value);
   }
   if (value.observed_endpoint) {
      const auto bytes = endpoint_bytes(*value.observed_endpoint);
      append_bytes(out, 4, bytes);
   }
   if (!value.protocol_version.empty()) {
      append_string(out, 5, value.protocol_version);
   }
   if (!value.agent_version.empty()) {
      append_string(out, 6, value.agent_version);
   }
   if (!value.signed_peer_record.empty()) {
      append_bytes(out, 8, value.signed_peer_record);
   }
   return out;
}

document decode(std::span<const std::uint8_t> bytes) {
   return decode(bytes, limits{});
}

document decode(std::span<const std::uint8_t> bytes, const limits& limits_value) {
   if (bytes.size() > limits_value.max_total_message_size) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "Identify message parts exceed aggregate size");
   }
   auto out = document{};
   auto in = reader{bytes};
   auto listen_address_count = std::size_t{};
   while (!in.done()) {
      const auto [field, type] = in.key();
      if (type != wire_type::length_delimited) {
         if (field >= 1 && field <= 8 && field != 7) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "Identify protobuf field has invalid wire type");
         }
         in.skip(type);
         continue;
      }
      switch (field) {
      case 1:
         out.public_key = in.bytes(limits_value.max_public_key_size);
         out.present.public_key = true;
         break;
      case 2: {
         if (listen_address_count++ >= limits_value.max_listen_endpoints) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "Identify has too many listen addresses");
         }
         try {
            auto endpoint = supported_endpoint(in.bytes(limits_value.max_message_size));
            if (endpoint) {
               out.listen_endpoints.push_back(std::move(*endpoint));
            }
         } catch (const forge::multiformats::exceptions::invalid_format& error) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, error.what());
         }
         break;
      }
      case 3: {
         if (out.protocols.size() >= limits_value.max_protocols) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "Identify has too many protocols");
         }
         auto protocol = in.string(limits_value.max_protocol_size);
         if (protocol.empty() || protocol.front() != '/') {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "Identify contains invalid protocol id");
         }
         out.protocols.push_back(protocol_id{.value = std::move(protocol)});
         break;
      }
      case 4:
         try {
            out.observed_endpoint = supported_endpoint(in.bytes(limits_value.max_message_size));
            out.present.observed_endpoint = true;
         } catch (const forge::multiformats::exceptions::invalid_format& error) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, error.what());
         }
         break;
      case 5:
         out.protocol_version = in.string(limits_value.max_version_size);
         out.present.protocol_version = true;
         break;
      case 6:
         out.agent_version = in.string(limits_value.max_version_size);
         out.present.agent_version = true;
         break;
      case 8:
         out.signed_peer_record = in.bytes(limits_value.max_signed_peer_record_size);
         out.present.signed_peer_record = true;
         break;
      default:
         in.skip(type);
         break;
      }
   }
   return out;
}

} // namespace forge::net::p2p::identify
