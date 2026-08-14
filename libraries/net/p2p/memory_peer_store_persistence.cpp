module;

#include <boost/asio/awaitable.hpp>

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.net.p2p.peer_store;

import forge.net.p2p.exceptions;

#include "details/memory_peer_store_persistence.hxx"

namespace forge::net::p2p {
namespace {

[[nodiscard]] char hex_digit(std::uint8_t value) {
   constexpr auto digits = std::string_view{"0123456789abcdef"};
   return digits[value & 0x0fU];
}

[[nodiscard]] std::string hex_text(std::string_view value) {
   auto result = std::string{};
   result.reserve(value.size() * 2U);
   for (const auto byte : value) {
      const auto unsigned_byte = static_cast<std::uint8_t>(byte);
      result.push_back(hex_digit(unsigned_byte >> 4U));
      result.push_back(hex_digit(unsigned_byte));
   }
   return result;
}

[[nodiscard]] std::string peer_token(const peer_id& peer) {
   return "0:" + hex_text(peer.value);
}

[[nodiscard]] std::string rendezvous_token(const rendezvous::registration& value) {
   return "2:" + hex_text(value.namespace_name) + ':' + hex_text(value.peer.value);
}

[[nodiscard]] std::vector<std::byte> cursor_bytes(std::string_view value) {
   auto out = std::vector<std::byte>(value.size());
   std::memcpy(out.data(), value.data(), value.size());
   return out;
}

[[nodiscard]] std::string cursor_text(const std::vector<std::byte>& value) {
   auto out = std::string(value.size(), '\0');
   std::memcpy(out.data(), value.data(), value.size());
   return out;
}

} // namespace

void memory_peer_store_persistence::ensure_open_locked() const {
   if (closed_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "memory peer store persistence is closed");
   }
}

boost::asio::awaitable<peer_store::hydration_page>
memory_peer_store_persistence::async_hydrate(peer_store::hydration_request request) {
   if (request.limit == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "hydration limit must be positive");
   }

   auto page = peer_store::hydration_page{};
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   page.rendezvous_sequence_high_watermark = rendezvous_sequence_high_watermark_;
   const auto after = request.cursor ? std::optional<std::string>{cursor_text(*request.cursor)} : std::nullopt;

   switch (request.kind) {
   case peer_store::hydration_kind::peers: {
      auto iterator = after ? peers_by_cursor_.upper_bound(*after) : peers_by_cursor_.begin();
      auto last_token = std::string{};
      for (auto count = std::size_t{}; iterator != peers_by_cursor_.end() && count < request.limit;
           ++iterator, ++count) {
         last_token = iterator->first;
         page.peers.push_back(peers_.at(iterator->second));
      }
      if (iterator != peers_by_cursor_.end()) {
         page.cursor = cursor_bytes(last_token);
      }
      break;
   }
   case peer_store::hydration_kind::rendezvous: {
      auto iterator = after ? rendezvous_by_cursor_.upper_bound(*after) : rendezvous_by_cursor_.begin();
      auto last_token = std::string{};
      for (auto count = std::size_t{}; iterator != rendezvous_by_cursor_.end() && count < request.limit;
           ++iterator, ++count) {
         last_token = iterator->first;
         page.rendezvous_registrations.push_back(rendezvous_.at(iterator->second));
      }
      if (iterator != rendezvous_by_cursor_.end()) {
         page.cursor = cursor_bytes(last_token);
      }
      break;
   }
   }
   co_return page;
}

boost::asio::awaitable<peer_store::apply_result>
memory_peer_store_persistence::async_apply(peer_store::mutation_batch batch) {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();

   auto peers = peers_;
   auto peers_by_cursor = peers_by_cursor_;
   auto peers_by_expiry = peers_by_expiry_;
   auto rendezvous_values = rendezvous_;
   auto rendezvous_by_cursor = rendezvous_by_cursor_;
   auto rendezvous_by_expiry = rendezvous_by_expiry_;
   auto high_watermark = std::max(rendezvous_sequence_high_watermark_, batch.rendezvous_sequence_high_watermark);
   for (auto& value : batch.peer_upserts) {
      if (const auto current = peers.find(value.peer);
          current != peers.end() && current->second.discovery_expires_at != std::chrono::system_clock::time_point{}) {
         peers_by_expiry.erase({current->second.discovery_expires_at, value.peer});
      }
      peers_by_cursor.insert_or_assign(peer_token(value.peer), value.peer);
      if (value.discovery_expires_at != std::chrono::system_clock::time_point{}) {
         peers_by_expiry.emplace(value.discovery_expires_at, value.peer);
      }
      peers.insert_or_assign(value.peer, std::move(value));
   }
   for (const auto& value : batch.peer_removals) {
      if (const auto current = peers.find(value);
          current != peers.end() && current->second.discovery_expires_at != std::chrono::system_clock::time_point{}) {
         peers_by_expiry.erase({current->second.discovery_expires_at, value});
      }
      peers_by_cursor.erase(peer_token(value));
      peers.erase(value);
   }
   for (auto& value : batch.rendezvous_upserts) {
      const auto key = rendezvous_map_key{value.namespace_name, value.peer};
      if (const auto current = rendezvous_values.find(key); current != rendezvous_values.end()) {
         rendezvous_by_expiry.erase({current->second.expires_at, key});
      }
      high_watermark = std::max(high_watermark, value.sequence);
      rendezvous_by_cursor.insert_or_assign(rendezvous_token(value), key);
      rendezvous_by_expiry.emplace(value.expires_at, key);
      rendezvous_values.insert_or_assign(key, std::move(value));
   }
   for (auto& value : batch.rendezvous_removals) {
      const auto key = rendezvous_map_key{value.namespace_name, value.peer};
      if (const auto current = rendezvous_values.find(key); current != rendezvous_values.end()) {
         rendezvous_by_expiry.erase({current->second.expires_at, key});
      }
      rendezvous_by_cursor.erase(rendezvous_token(rendezvous::registration{
          .namespace_name = value.namespace_name,
          .peer = value.peer,
      }));
      rendezvous_values.erase(key);
   }

   peers_.swap(peers);
   peers_by_cursor_.swap(peers_by_cursor);
   peers_by_expiry_.swap(peers_by_expiry);
   rendezvous_.swap(rendezvous_values);
   rendezvous_by_cursor_.swap(rendezvous_by_cursor);
   rendezvous_by_expiry_.swap(rendezvous_by_expiry);
   rendezvous_sequence_high_watermark_ = high_watermark;
   co_return peer_store::apply_result{};
}

boost::asio::awaitable<peer_store::prune_result>
memory_peer_store_persistence::async_prune_expired(std::chrono::system_clock::time_point now, std::size_t limit) {
   if (limit == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "prune limit must be positive");
   }

   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   auto result = peer_store::prune_result{};
   auto removed = std::size_t{};
   while (removed < limit && !peers_by_expiry_.empty() && peers_by_expiry_.begin()->first <= now) {
      const auto peer = peers_by_expiry_.begin()->second;
      peers_by_expiry_.erase(peers_by_expiry_.begin());
      peers_by_cursor_.erase(peer_token(peer));
      if (peers_.erase(peer) == 0) {
         continue;
      }
      result.peers.push_back(peer);
      ++removed;
   }
   while (removed < limit && !rendezvous_by_expiry_.empty() && rendezvous_by_expiry_.begin()->first <= now) {
      const auto key = rendezvous_by_expiry_.begin()->second;
      rendezvous_by_expiry_.erase(rendezvous_by_expiry_.begin());
      const auto current = rendezvous_.find(key);
      if (current == rendezvous_.end()) {
         continue;
      }
      result.rendezvous_registrations.push_back(current->second);
      rendezvous_by_cursor_.erase(rendezvous_token(current->second));
      rendezvous_.erase(current);
      ++removed;
   }
   result.may_have_more = (!peers_by_expiry_.empty() && peers_by_expiry_.begin()->first <= now) ||
                          (!rendezvous_by_expiry_.empty() && rendezvous_by_expiry_.begin()->first <= now);
   co_return result;
}

boost::asio::awaitable<void> memory_peer_store_persistence::async_flush() {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   co_return;
}

boost::asio::awaitable<void> memory_peer_store_persistence::async_close() {
   auto lock = std::scoped_lock{mutex_};
   closed_ = true;
   co_return;
}

std::shared_ptr<peer_store::persistence> peer_store::make_memory_persistence() {
   return std::make_shared<memory_peer_store_persistence>();
}

} // namespace forge::net::p2p
