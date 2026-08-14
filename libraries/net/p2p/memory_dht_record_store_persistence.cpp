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
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

module forge.net.p2p.dht.record_store;

import forge.net.p2p.exceptions;

#include "details/memory_dht_record_store_persistence.hxx"

namespace forge::net::p2p {
namespace {

[[nodiscard]] char hex_digit(std::uint8_t value) {
   constexpr auto digits = std::string_view{"0123456789abcdef"};
   return digits[value & 0x0fU];
}

[[nodiscard]] std::string hex_bytes(const std::vector<std::uint8_t>& value) {
   auto result = std::string{};
   result.reserve(value.size() * 2U);
   for (const auto byte : value) {
      result.push_back(hex_digit(byte >> 4U));
      result.push_back(hex_digit(byte));
   }
   return result;
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

[[nodiscard]] std::string value_token(const dht::record_store::value_record& value) {
   return hex_bytes(value.record.key_value.bytes);
}

[[nodiscard]] std::string provider_token(const dht::record_store::provider_record& value) {
   return hex_bytes(value.key.bytes) + ':' + hex_text(value.provider.value);
}

[[nodiscard]] std::vector<std::byte> cursor_bytes(std::string_view value) {
   auto result = std::vector<std::byte>(value.size());
   std::memcpy(result.data(), value.data(), value.size());
   return result;
}

[[nodiscard]] std::string cursor_text(const std::vector<std::byte>& value) {
   auto result = std::string(value.size(), '\0');
   std::memcpy(result.data(), value.data(), value.size());
   return result;
}

} // namespace

void memory_dht_record_store_persistence::ensure_open_locked() const {
   if (closed_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "memory DHT record store persistence is closed");
   }
}

boost::asio::awaitable<dht::record_store::hydration_page>
memory_dht_record_store_persistence::async_hydrate(dht::record_store::hydration_request request) {
   if (request.limit == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT hydration limit must be positive");
   }
   auto page = dht::record_store::hydration_page{};
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   const auto after = request.cursor ? std::optional<std::string>{cursor_text(*request.cursor)} : std::nullopt;

   switch (request.kind) {
   case dht::record_store::hydration_kind::values: {
      auto iterator = after ? values_by_cursor_.upper_bound(*after) : values_by_cursor_.begin();
      auto last_token = std::string{};
      for (auto count = std::size_t{}; iterator != values_by_cursor_.end() && count < request.limit;
           ++iterator, ++count) {
         last_token = iterator->first;
         page.values.push_back(values_.at(iterator->second));
      }
      if (iterator != values_by_cursor_.end()) {
         page.cursor = cursor_bytes(last_token);
      }
      break;
   }
   case dht::record_store::hydration_kind::providers: {
      auto iterator = after ? providers_by_cursor_.upper_bound(*after) : providers_by_cursor_.begin();
      auto last_token = std::string{};
      for (auto count = std::size_t{}; iterator != providers_by_cursor_.end() && count < request.limit;
           ++iterator, ++count) {
         last_token = iterator->first;
         page.providers.push_back(providers_.at(iterator->second));
      }
      if (iterator != providers_by_cursor_.end()) {
         page.cursor = cursor_bytes(last_token);
      }
      break;
   }
   }
   co_return page;
}

boost::asio::awaitable<dht::record_store::apply_result>
memory_dht_record_store_persistence::async_apply(dht::record_store::mutation_batch batch) {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   if (fail_next_apply_) {
      fail_next_apply_ = false;
      throw std::runtime_error{"injected DHT record persistence failure"};
   }

   auto values = values_;
   auto values_by_cursor = values_by_cursor_;
   auto values_by_expiry = values_by_expiry_;
   auto providers = providers_;
   auto providers_by_cursor = providers_by_cursor_;
   auto providers_by_expiry = providers_by_expiry_;
   auto provider_addresses_by_expiry = provider_addresses_by_expiry_;

   const auto erase_value = [&](const std::vector<std::uint8_t>& key) {
      const auto current = values.find(key);
      if (current == values.end()) {
         return;
      }
      values_by_cursor.erase(value_token(current->second));
      if (current->second.expires_at != std::chrono::system_clock::time_point{}) {
         values_by_expiry.erase({current->second.expires_at, key});
      }
      values.erase(current);
   };
   const auto erase_provider = [&](const provider_map_key& key) {
      const auto current = providers.find(key);
      if (current == providers.end()) {
         return;
      }
      providers_by_cursor.erase(provider_token(current->second));
      if (current->second.provider_expires_at != std::chrono::system_clock::time_point{}) {
         providers_by_expiry.erase(
             {current->second.provider_expires_at, current->second.key.bytes, current->second.provider});
      }
      if (current->second.addresses_expires_at != std::chrono::system_clock::time_point{} &&
          !current->second.endpoints.empty()) {
         provider_addresses_by_expiry.erase(
             {current->second.addresses_expires_at, current->second.key.bytes, current->second.provider});
      }
      providers.erase(current);
   };

   for (const auto& key : batch.value_removals) {
      erase_value(key.bytes);
   }
   for (const auto& key : batch.provider_removals) {
      erase_provider(provider_map_key{key.key.bytes, key.provider});
   }
   for (auto& value : batch.value_upserts) {
      const auto key = value.record.key_value.bytes;
      erase_value(key);
      values_by_cursor.emplace(value_token(value), key);
      if (value.expires_at != std::chrono::system_clock::time_point{}) {
         values_by_expiry.emplace(value.expires_at, key);
      }
      values.emplace(key, std::move(value));
   }
   for (auto& value : batch.provider_upserts) {
      const auto key = provider_map_key{value.key.bytes, value.provider};
      erase_provider(key);
      providers_by_cursor.emplace(provider_token(value), key);
      if (value.provider_expires_at != std::chrono::system_clock::time_point{}) {
         providers_by_expiry.emplace(value.provider_expires_at, value.key.bytes, value.provider);
      }
      if (value.addresses_expires_at != std::chrono::system_clock::time_point{} && !value.endpoints.empty()) {
         provider_addresses_by_expiry.emplace(value.addresses_expires_at, value.key.bytes, value.provider);
      }
      providers.emplace(key, std::move(value));
   }

   values_.swap(values);
   values_by_cursor_.swap(values_by_cursor);
   values_by_expiry_.swap(values_by_expiry);
   providers_.swap(providers);
   providers_by_cursor_.swap(providers_by_cursor);
   providers_by_expiry_.swap(providers_by_expiry);
   provider_addresses_by_expiry_.swap(provider_addresses_by_expiry);
   co_return dht::record_store::apply_result{};
}

boost::asio::awaitable<dht::record_store::prune_result>
memory_dht_record_store_persistence::async_prune_expired(std::chrono::system_clock::time_point now, std::size_t limit) {
   if (limit == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT prune limit must be positive");
   }
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();

   auto values = values_;
   auto values_by_cursor = values_by_cursor_;
   auto values_by_expiry = values_by_expiry_;
   auto providers = providers_;
   auto providers_by_cursor = providers_by_cursor_;
   auto providers_by_expiry = providers_by_expiry_;
   auto provider_addresses_by_expiry = provider_addresses_by_expiry_;
   auto result = dht::record_store::prune_result{};

   for (auto count = std::size_t{}; count < limit;) {
      const auto value_expiry =
          values_by_expiry.empty() ? std::chrono::system_clock::time_point::max() : values_by_expiry.begin()->first;
      const auto provider_expiry = providers_by_expiry.empty() ? std::chrono::system_clock::time_point::max()
                                                               : std::get<0>(*providers_by_expiry.begin());
      const auto addresses_expiry = provider_addresses_by_expiry.empty()
                                        ? std::chrono::system_clock::time_point::max()
                                        : std::get<0>(*provider_addresses_by_expiry.begin());
      const auto next_expiry = std::min({value_expiry, provider_expiry, addresses_expiry});
      if (next_expiry > now) {
         break;
      }

      if (value_expiry == next_expiry) {
         const auto key = values_by_expiry.begin()->second;
         values_by_expiry.erase(values_by_expiry.begin());
         const auto current = values.find(key);
         if (current == values.end() || current->second.expires_at != value_expiry) {
            continue;
         }
         result.values.push_back(current->second.record.key_value);
         values_by_cursor.erase(value_token(current->second));
         values.erase(current);
         ++count;
         continue;
      }

      if (provider_expiry == next_expiry) {
         const auto event = *providers_by_expiry.begin();
         providers_by_expiry.erase(providers_by_expiry.begin());
         const auto key = provider_map_key{std::get<1>(event), std::get<2>(event)};
         const auto current = providers.find(key);
         if (current == providers.end() || current->second.provider_expires_at != provider_expiry) {
            continue;
         }
         result.providers.push_back(
             dht::record_store::provider_key{.key = current->second.key, .provider = current->second.provider});
         providers_by_cursor.erase(provider_token(current->second));
         if (current->second.addresses_expires_at != std::chrono::system_clock::time_point{} &&
             !current->second.endpoints.empty()) {
            provider_addresses_by_expiry.erase(
                {current->second.addresses_expires_at, current->second.key.bytes, current->second.provider});
         }
         providers.erase(current);
         ++count;
         continue;
      }

      const auto event = *provider_addresses_by_expiry.begin();
      provider_addresses_by_expiry.erase(provider_addresses_by_expiry.begin());
      const auto key = provider_map_key{std::get<1>(event), std::get<2>(event)};
      const auto current = providers.find(key);
      if (current == providers.end() || current->second.addresses_expires_at != addresses_expiry) {
         continue;
      }
      if (current->second.provider_expires_at <= now) {
         result.providers.push_back(
             dht::record_store::provider_key{.key = current->second.key, .provider = current->second.provider});
         providers_by_expiry.erase(
             {current->second.provider_expires_at, current->second.key.bytes, current->second.provider});
         providers_by_cursor.erase(provider_token(current->second));
         providers.erase(current);
         ++count;
         continue;
      }
      current->second.endpoints.clear();
      current->second.addresses_expires_at = {};
      result.provider_address_updates.push_back(current->second);
      ++count;
   }

   const auto value_more = !values_by_expiry.empty() && values_by_expiry.begin()->first <= now;
   const auto provider_more = !providers_by_expiry.empty() && std::get<0>(*providers_by_expiry.begin()) <= now;
   const auto addresses_more =
       !provider_addresses_by_expiry.empty() && std::get<0>(*provider_addresses_by_expiry.begin()) <= now;
   result.may_have_more = value_more || provider_more || addresses_more;

   values_.swap(values);
   values_by_cursor_.swap(values_by_cursor);
   values_by_expiry_.swap(values_by_expiry);
   providers_.swap(providers);
   providers_by_cursor_.swap(providers_by_cursor);
   providers_by_expiry_.swap(providers_by_expiry);
   provider_addresses_by_expiry_.swap(provider_addresses_by_expiry);
   co_return result;
}

boost::asio::awaitable<void> memory_dht_record_store_persistence::async_flush() {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   co_return;
}

boost::asio::awaitable<void> memory_dht_record_store_persistence::async_close() {
   auto lock = std::scoped_lock{mutex_};
   closed_ = true;
   co_return;
}

void memory_dht_record_store_persistence::fail_next_apply_for_testing() {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   fail_next_apply_ = true;
}

std::shared_ptr<dht::record_store::persistence> make_memory_dht_record_store_persistence() {
   return std::make_shared<memory_dht_record_store_persistence>();
}

} // namespace forge::net::p2p
