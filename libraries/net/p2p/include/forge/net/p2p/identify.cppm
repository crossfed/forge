module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <span>
#include <vector>

export module forge.net.p2p.identify;

import forge.multiformats.types;
import forge.net.p2p.endpoint;
import forge.net.p2p.protocol;

export namespace forge::net::p2p {

namespace identify {
enum class state : std::uint8_t {
   unknown = 0,
   identifying = 1,
   identified = 2,
   failed = 3,
};

struct limits {
   std::chrono::milliseconds timeout{5'000};
   std::size_t max_message_size = 8 * 1024;
   std::size_t max_total_message_size = 80 * 1024;
   std::size_t max_own_message_size = 4 * 1024;
   std::size_t max_message_parts = 10;
   std::size_t max_protocols = 128;
   std::size_t max_listen_endpoints = 64;
   std::size_t max_protocol_size = 1'024;
   std::size_t max_version_size = 512;
   std::size_t max_public_key_size = 4 * 1024;
   std::size_t max_signed_peer_record_size = 8 * 1024;
   std::size_t max_push_operations = 32;
};

struct document_presence {
   bool public_key = false;
   bool observed_endpoint = false;
   bool protocol_version = false;
   bool agent_version = false;
   bool signed_peer_record = false;
};

   struct document {
      std::string protocol_version;
      std::string agent_version;
      std::vector<std::uint8_t> public_key;
      std::vector<endpoint> listen_endpoints;
      std::optional<endpoint> observed_endpoint;
      std::vector<protocol_id> protocols;
      std::vector<std::uint8_t> signed_peer_record;
      document_presence present;
   };

   [[nodiscard]] forge::multiformats::bytes encode(const document& value);
   [[nodiscard]] document decode(std::span<const std::uint8_t> bytes);
   [[nodiscard]] document decode(std::span<const std::uint8_t> bytes, const limits& limits_value);
} // namespace identify

} // namespace forge::net::p2p
