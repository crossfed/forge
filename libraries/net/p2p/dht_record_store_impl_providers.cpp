module;

#include <boost/asio/awaitable.hpp>

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

module forge.net.p2p.dht.record_store;

import forge.asio.gate;
import forge.net.p2p.exceptions;

#include "details/dht_record_store_impl.hxx"

namespace forge::net::p2p {

std::size_t dht::record_store::impl::provider_bytes(const dht::record_store::provider_record& value) const {
   auto result = value.key.bytes.size() + value.provider.value.size();
   for (const auto& endpoint_value : value.endpoints) {
      const auto text = endpoint_value.to_string();
      if (text.size() > std::numeric_limits<std::size_t>::max() - result) {
         return std::numeric_limits<std::size_t>::max();
      }
      result += text.size();
   }
   return result;
}

void dht::record_store::impl::validate_provider(const dht::record_store::provider_record& value,
                                                std::chrono::system_clock::time_point now) const {
   if (!profile_.capabilities.providers) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "DHT profile does not enable provider records");
   }
   if (value.key.bytes.empty() || !valid_peer_id(value.provider)) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT provider record key and Peer ID must be valid");
   }
   if (value.provider_expires_at == std::chrono::system_clock::time_point{} || value.provider_expires_at <= now) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT provider expiry must be in the future");
   }
   if (!value.endpoints.empty() && value.addresses_expires_at == std::chrono::system_clock::time_point{}) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT provider addresses require an explicit expiry");
   }
   if (provider_bytes(value) > options_.max_record_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT provider record byte limit exceeded");
   }
}

void dht::record_store::impl::ensure_provider_capacity_locked(const dht::record_store::provider_record& value) const {
   const auto key = provider_map_key{value.key.bytes, value.provider};
   const auto current = providers_.find(key);
   if (current == providers_.end()) {
      if (providers_.size() >= options_.max_providers) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT provider capacity reached");
      }
      const auto by_key = providers_by_key_.find(value.key.bytes);
      const auto per_key = by_key == providers_by_key_.end() ? 0U : by_key->second.size();
      if (per_key >= options_.max_providers_per_key) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT provider per-key capacity reached");
      }
      if (value.local_owned && local_providers_ >= options_.max_local_providers) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT local provider capacity reached");
      }
   } else if (!current->second.local_owned && value.local_owned && local_providers_ >= options_.max_local_providers) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT local provider capacity reached");
   }
   const auto old_bytes = current == providers_.end() ? 0U : provider_bytes(current->second);
   const auto new_bytes = provider_bytes(value);
   if (exceeds(total_bytes_, old_bytes, new_bytes, options_.max_total_bytes)) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT record store total byte capacity reached");
   }
}

void dht::record_store::impl::publish_provider_locked(dht::record_store::provider_record value) {
   const auto key = provider_map_key{value.key.bytes, value.provider};
   if (const auto current = providers_.find(key); current != providers_.end()) {
      total_bytes_ -= provider_bytes(current->second);
      if (current->second.local_owned && !value.local_owned) {
         --local_providers_;
      } else if (!current->second.local_owned && value.local_owned) {
         ++local_providers_;
      }
   } else {
      providers_by_key_[value.key.bytes].insert(value.provider);
      if (value.local_owned) {
         ++local_providers_;
      }
   }
   total_bytes_ += provider_bytes(value);
   providers_.insert_or_assign(key, std::move(value));
}

void dht::record_store::impl::erase_provider_locked(const dht::record_store::provider_key& key) {
   const auto map_key = provider_map_key{key.key.bytes, key.provider};
   const auto current = providers_.find(map_key);
   if (current == providers_.end()) {
      return;
   }
   total_bytes_ -= provider_bytes(current->second);
   if (current->second.local_owned) {
      --local_providers_;
   }
   providers_.erase(current);
   const auto by_key = providers_by_key_.find(key.key.bytes);
   if (by_key != providers_by_key_.end()) {
      by_key->second.erase(key.provider);
      if (by_key->second.empty()) {
         providers_by_key_.erase(by_key);
      }
   }
}

boost::asio::awaitable<void> dht::record_store::impl::async_upsert_provider(dht::record_store::provider_record value,
                                                                            std::chrono::system_clock::time_point now) {
   auto admission = admit_operation();
   auto ticket = co_await persistence_gate_.acquire();
   if (expired(value.addresses_expires_at, now)) {
      value.endpoints.clear();
      value.addresses_expires_at = {};
   }
   validate_provider(value, now);
   {
      auto lock = std::scoped_lock{mutex_};
      ensure_open_locked();
      ensure_provider_capacity_locked(value);
   }
   auto batch = dht::record_store::mutation_batch{};
   batch.provider_upserts.push_back(value);
   auto result = dht::record_store::apply_result{};
   try {
      result = co_await persistence_->async_apply(std::move(batch));
   } catch (...) {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked(current_failure_message());
      throw;
   }
   {
      auto lock = std::scoped_lock{mutex_};
      publish_provider_locked(std::move(value));
      apply_durability_result_locked(result);
   }
   if (!result.durability_confirmed) {
      throw_durability_uncertain(result);
   }
}

boost::asio::awaitable<void> dht::record_store::impl::async_remove_provider(dht::record_store::provider_key key) {
   auto admission = admit_operation();
   auto ticket = co_await persistence_gate_.acquire();
   {
      auto lock = std::scoped_lock{mutex_};
      ensure_open_locked();
   }
   if (key.key.bytes.empty() || !valid_peer_id(key.provider)) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT provider removal key and Peer ID must be valid");
   }
   auto batch = dht::record_store::mutation_batch{};
   batch.provider_removals.push_back(key);
   auto result = dht::record_store::apply_result{};
   try {
      result = co_await persistence_->async_apply(std::move(batch));
   } catch (...) {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked(current_failure_message());
      throw;
   }
   {
      auto lock = std::scoped_lock{mutex_};
      erase_provider_locked(key);
      apply_durability_result_locked(result);
   }
   if (!result.durability_confirmed) {
      throw_durability_uncertain(result);
   }
}

std::vector<dht::record_store::provider_record>
dht::record_store::impl::find_providers(const dht::key& key, std::size_t limit,
                                        std::chrono::system_clock::time_point now) const {
   auto result = std::vector<dht::record_store::provider_record>{};
   result.reserve(std::min(limit, options_.max_providers_per_key));
   auto lock = std::scoped_lock{mutex_};
   const auto by_key = providers_by_key_.find(key.bytes);
   if (by_key == providers_by_key_.end()) {
      return result;
   }
   for (const auto& provider : by_key->second) {
      if (result.size() == limit) {
         break;
      }
      const auto current = providers_.find(provider_map_key{key.bytes, provider});
      if (current == providers_.end() || expired(current->second.provider_expires_at, now)) {
         continue;
      }
      auto value = current->second;
      if (expired(value.addresses_expires_at, now)) {
         value.endpoints.clear();
      }
      result.push_back(std::move(value));
   }
   return result;
}

} // namespace forge::net::p2p
