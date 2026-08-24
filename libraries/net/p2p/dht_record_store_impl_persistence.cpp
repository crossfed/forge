module;

#include <boost/asio/awaitable.hpp>

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iterator>
#include <map>
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

void dht::record_store::impl::mark_persistence_failure_locked(std::string message) {
   degraded_ = true;
   ++persistence_failures_;
   last_failure_ = std::move(message);
}

void dht::record_store::impl::mark_durability_uncertain_locked(std::string message) {
   durability_uncertain_ = true;
   mark_persistence_failure_locked(std::move(message));
}

void dht::record_store::impl::mark_persistence_healthy_locked(bool durability_confirmed) {
   if (durability_confirmed && !reconciliation_required_) {
      durability_uncertain_ = false;
   }
   if (durability_uncertain_) {
      return;
   }
   degraded_ = false;
   last_failure_.clear();
}

void dht::record_store::impl::apply_durability_result_locked(const dht::record_store::apply_result& result) {
   if (result.durability_confirmed) {
      mark_persistence_healthy_locked(true);
   } else {
      mark_durability_uncertain_locked(durability_failure_message(result));
   }
}

void dht::record_store::impl::validate_hydration_prune_result(const dht::record_store::prune_result& result) const {
   const auto empty = result.values.empty() && result.providers.empty() && result.provider_address_updates.empty();
   if (result.values.size() > options_.prune_page_limit ||
       result.providers.size() > options_.prune_page_limit - result.values.size() ||
       result.provider_address_updates.size() >
           options_.prune_page_limit - result.values.size() - result.providers.size() ||
       (result.may_have_more && empty)) {
      FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence returned an invalid hydration prune result");
   }

   auto value_removals = std::set<value_key>{};
   for (const auto& key : result.values) {
      if (key.bytes.empty() || !value_removals.insert(key.bytes).second) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence returned an invalid hydration value prune key");
      }
   }

   auto provider_removals = std::set<provider_map_key>{};
   for (const auto& key : result.providers) {
      const auto map_key = provider_map_key{key.key.bytes, key.provider};
      if (key.key.bytes.empty() || !valid_peer_id(key.provider) || !provider_removals.insert(map_key).second) {
         FORGE_THROW_EXCEPTION(exceptions::internal,
                               "DHT persistence returned an invalid hydration provider prune key");
      }
   }

   auto address_updates = std::set<provider_map_key>{};
   for (const auto& value : result.provider_address_updates) {
      const auto key = provider_map_key{value.key.bytes, value.provider};
      if (value.key.bytes.empty() || !valid_peer_id(value.provider) || !address_updates.insert(key).second ||
          provider_removals.contains(key) || !value.endpoints.empty() ||
          value.addresses_expires_at != std::chrono::system_clock::time_point{}) {
         FORGE_THROW_EXCEPTION(exceptions::internal,
                               "DHT persistence returned an invalid hydration provider address update");
      }
   }
}

boost::asio::awaitable<bool>
dht::record_store::impl::async_prune_persistence_for_hydration(std::chrono::system_clock::time_point now) {
   auto changed = false;
   for (auto page = std::size_t{}; page < options_.max_hydration_pages; ++page) {
      auto result = dht::record_store::prune_result{};
      try {
         result = co_await persistence_->async_prune_expired(now, options_.prune_page_limit);
      } catch (...) {
         auto lock = std::scoped_lock{mutex_};
         mark_persistence_failure_locked(current_failure_message());
         throw;
      }

      try {
         validate_hydration_prune_result(result);
      } catch (...) {
         auto lock = std::scoped_lock{mutex_};
         reconciliation_required_ = true;
         mark_durability_uncertain_locked("DHT persistence returned an invalid committed hydration prune result");
         throw;
      }
      if (!result.durability.durability_confirmed) {
         {
            auto lock = std::scoped_lock{mutex_};
            apply_durability_result_locked(result.durability);
         }
         throw_durability_uncertain(result.durability);
      }
      const auto page_changed =
          !result.values.empty() || !result.providers.empty() || !result.provider_address_updates.empty();
      if (page_changed) {
         auto lock = std::scoped_lock{mutex_};
         mark_persistence_healthy_locked(true);
      }
      changed = changed || page_changed;
      if (!result.may_have_more) {
         co_return changed;
      }
   }
   {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked("DHT hydration prune page budget exhausted");
   }
   FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT hydration prune page budget exhausted");
}

boost::asio::awaitable<void> dht::record_store::impl::async_hydrate(std::chrono::system_clock::time_point now) {
   auto admission = admit_operation();
   auto ticket = co_await persistence_gate_.acquire();
   auto hydrated_values = std::vector<dht::record_store::value_record>{};
   auto hydrated_providers = std::vector<dht::record_store::provider_record>{};
   auto expired_values = std::vector<dht::key>{};
   auto stale_local_providers = std::vector<dht::record_store::provider_key>{};
   auto expired_providers = std::vector<dht::record_store::provider_key>{};

   const auto pruned_persistence = co_await async_prune_persistence_for_hydration(now);

   const auto hydrate_kind = [this, now, &hydrated_values, &hydrated_providers, &expired_values, &stale_local_providers,
                              &expired_providers](dht::record_store::hydration_kind kind,
                                                  std::size_t maximum) -> boost::asio::awaitable<void> {
      auto cursor = std::optional<std::vector<std::byte>>{};
      auto retained = std::size_t{};
      for (auto page_index = std::size_t{}; page_index < options_.max_hydration_pages; ++page_index) {
         const auto limit = options_.hydration_page_limit;
         auto page = co_await persistence_->async_hydrate(
             dht::record_store::hydration_request{.kind = kind, .cursor = cursor, .limit = limit});
         const auto wrong_kind = (kind != dht::record_store::hydration_kind::values && !page.values.empty()) ||
                                 (kind != dht::record_store::hydration_kind::providers && !page.providers.empty());
         const auto oversized = page.values.size() > limit || page.providers.size() > limit - page.values.size();
         const auto empty = page.values.empty() && page.providers.empty();
         if (oversized || wrong_kind || (page.cursor && (page.cursor == cursor || empty))) {
            FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence returned an invalid hydration page");
         }

         for (auto& value : page.values) {
            if (expired(value.expires_at, now)) {
               expired_values.push_back(value.record.key_value);
               continue;
            }
            if (retained == maximum) {
               FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT hydration capacity reached");
            }
            hydrated_values.push_back(std::move(value));
            ++retained;
         }
         for (auto& value : page.providers) {
            auto key = dht::record_store::provider_key{.key = value.key, .provider = value.provider};
            if (value.local_owned) {
               stale_local_providers.push_back(std::move(key));
               continue;
            }
            if (expired(value.provider_expires_at, now)) {
               expired_providers.push_back(std::move(key));
               continue;
            }
            if (retained == maximum) {
               FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT hydration capacity reached");
            }
            hydrated_providers.push_back(std::move(value));
            ++retained;
         }
         if (!page.cursor) {
            co_return;
         }
         cursor = std::move(page.cursor);
      }
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT hydration page budget exhausted");
   };

   try {
      co_await hydrate_kind(dht::record_store::hydration_kind::values, options_.max_values);
      co_await hydrate_kind(dht::record_store::hydration_kind::providers, options_.max_providers);
   } catch (...) {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked(current_failure_message());
      throw;
   }

   auto values = std::map<value_key, dht::record_store::value_record>{};
   auto providers = std::map<provider_map_key, dht::record_store::provider_record>{};
   auto providers_by_key = std::map<value_key, std::set<peer_id>>{};
   auto total_bytes = std::size_t{};
   auto expiry_updates = std::vector<dht::record_store::value_record>{};
   auto provider_updates = std::vector<dht::record_store::provider_record>{};
   for (auto& value : hydrated_values) {
      if (expired(value.expires_at, now)) {
         expired_values.push_back(value.record.key_value);
         continue;
      }
      const auto persisted_expiry = value.expires_at;
      static_cast<void>(prepare_value(value, now, false));
      if (value.expires_at != persisted_expiry) {
         expiry_updates.push_back(value);
      }
      const auto bytes = value_bytes(value);
      if (!values.emplace(value.record.key_value.bytes, value).second) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence hydrated a duplicate value key");
      }
      if (exceeds(total_bytes, 0, bytes, options_.max_total_bytes)) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT hydration byte capacity reached");
      }
      total_bytes += bytes;
   }
   for (auto& value : hydrated_providers) {
      if (value.local_owned) {
         stale_local_providers.push_back(dht::record_store::provider_key{.key = value.key, .provider = value.provider});
         continue;
      }
      if (expired(value.provider_expires_at, now)) {
         expired_providers.push_back(dht::record_store::provider_key{.key = value.key, .provider = value.provider});
         continue;
      }
      if (expired(value.addresses_expires_at, now)) {
         const auto requires_update =
             !value.endpoints.empty() || value.addresses_expires_at != std::chrono::system_clock::time_point{};
         value.endpoints.clear();
         value.addresses_expires_at = {};
         if (requires_update) {
            provider_updates.push_back(value);
         }
      }
      validate_provider(value, now);
      const auto key = provider_map_key{value.key.bytes, value.provider};
      if (!providers.emplace(key, value).second) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence hydrated a duplicate provider key");
      }
      auto& per_key = providers_by_key[value.key.bytes];
      if (per_key.size() >= options_.max_providers_per_key) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT hydration provider per-key capacity reached");
      }
      per_key.insert(value.provider);
      const auto bytes = provider_bytes(value);
      if (exceeds(total_bytes, 0, bytes, options_.max_total_bytes)) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "DHT hydration byte capacity reached");
      }
      total_bytes += bytes;
   }

   auto cleanup_result = std::optional<dht::record_store::apply_result>{};
   if (!expiry_updates.empty() || !expired_values.empty() || !provider_updates.empty() ||
       !stale_local_providers.empty() || !expired_providers.empty()) {
      auto batch = dht::record_store::mutation_batch{};
      batch.value_upserts = std::move(expiry_updates);
      batch.value_removals = std::move(expired_values);
      batch.provider_upserts = std::move(provider_updates);
      batch.provider_removals = std::move(stale_local_providers);
      batch.provider_removals.insert(batch.provider_removals.end(), std::make_move_iterator(expired_providers.begin()),
                                     std::make_move_iterator(expired_providers.end()));
      try {
         cleanup_result = co_await persistence_->async_apply(std::move(batch));
      } catch (...) {
         auto lock = std::scoped_lock{mutex_};
         mark_persistence_failure_locked(current_failure_message());
         throw;
      }
   }

   {
      auto lock = std::scoped_lock{mutex_};
      ensure_open_locked();
      values_.swap(values);
      providers_.swap(providers);
      providers_by_key_.swap(providers_by_key);
      local_providers_ = 0;
      total_bytes_ = total_bytes;
      reconciliation_required_ = false;
      if (cleanup_result) {
         apply_durability_result_locked(*cleanup_result);
      } else {
         // Hydration reconciles runtime state with committed persistence, but
         // only a mutation or flush can confirm that persistence is durable.
         mark_persistence_healthy_locked(pruned_persistence);
      }
   }
   if (cleanup_result && !cleanup_result->durability_confirmed) {
      throw_durability_uncertain(*cleanup_result);
   }
}

void dht::record_store::impl::apply_prune_locked(const dht::record_store::prune_result& result) {
   for (const auto& key : result.values) {
      erase_value_locked(key);
   }
   for (const auto& key : result.providers) {
      erase_provider_locked(key);
   }
   for (const auto& value : result.provider_address_updates) {
      const auto key = provider_map_key{value.key.bytes, value.provider};
      if (providers_.contains(key)) {
         publish_provider_locked(value);
      }
   }
}

void dht::record_store::impl::validate_prune_result_locked(const dht::record_store::prune_result& result,
                                                           std::chrono::system_clock::time_point now) const {
   auto value_removals = std::set<value_key>{};
   for (const auto& key : result.values) {
      if (key.bytes.empty() || !value_removals.insert(key.bytes).second) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence returned an invalid value prune key");
      }
      const auto current = values_.find(key.bytes);
      if (current == values_.end() || !expired(current->second.expires_at, now)) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence attempted to prune a live value record");
      }
   }

   auto provider_removals = std::set<provider_map_key>{};
   for (const auto& key : result.providers) {
      const auto map_key = provider_map_key{key.key.bytes, key.provider};
      if (key.key.bytes.empty() || !valid_peer_id(key.provider) || !provider_removals.insert(map_key).second) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence returned an invalid provider prune key");
      }
      const auto current = providers_.find(map_key);
      if (current == providers_.end() || !expired(current->second.provider_expires_at, now)) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence attempted to prune a live provider record");
      }
   }

   auto address_updates = std::set<provider_map_key>{};
   for (const auto& value : result.provider_address_updates) {
      const auto key = provider_map_key{value.key.bytes, value.provider};
      if (!address_updates.insert(key).second || provider_removals.contains(key)) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence returned conflicting provider prune results");
      }
      const auto current = providers_.find(key);
      if (current == providers_.end() || expired(current->second.provider_expires_at, now) ||
          !expired(current->second.addresses_expires_at, now) || value.key != current->second.key ||
          value.provider != current->second.provider ||
          value.provider_expires_at != current->second.provider_expires_at ||
          value.local_owned != current->second.local_owned || !value.endpoints.empty() ||
          value.addresses_expires_at != std::chrono::system_clock::time_point{}) {
         FORGE_THROW_EXCEPTION(exceptions::internal,
                               "DHT persistence returned an invalid provider address prune update");
      }
   }
}

boost::asio::awaitable<dht::record_store::prune_result>
dht::record_store::impl::async_prune_expired(std::chrono::system_clock::time_point now) {
   auto admission = admit_operation();
   auto ticket = co_await persistence_gate_.acquire();
   auto result = dht::record_store::prune_result{};
   try {
      result = co_await persistence_->async_prune_expired(now, options_.prune_page_limit);
   } catch (...) {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked(current_failure_message());
      throw;
   }
   {
      auto lock = std::scoped_lock{mutex_};
      try {
         if (result.values.size() > options_.prune_page_limit ||
             result.providers.size() > options_.prune_page_limit - result.values.size() ||
             result.provider_address_updates.size() >
                 options_.prune_page_limit - result.values.size() - result.providers.size()) {
            FORGE_THROW_EXCEPTION(exceptions::internal, "DHT persistence exceeded the prune result bound");
         }
         validate_prune_result_locked(result, now);
      } catch (...) {
         reconciliation_required_ = true;
         mark_durability_uncertain_locked("DHT persistence returned an invalid committed prune result");
         throw;
      }
      apply_prune_locked(result);
      const auto changed =
          !result.values.empty() || !result.providers.empty() || !result.provider_address_updates.empty();
      if (changed || !result.durability.durability_confirmed) {
         apply_durability_result_locked(result.durability);
      } else {
         mark_persistence_healthy_locked(false);
      }
   }
   if (!result.durability.durability_confirmed) {
      throw_durability_uncertain(result.durability);
   }
   co_return result;
}

boost::asio::awaitable<void> dht::record_store::impl::async_flush() {
   auto admission = admit_operation();
   auto ticket = co_await persistence_gate_.acquire();
   try {
      co_await persistence_->async_flush();
   } catch (...) {
      auto result = dht::record_store::apply_result{
          .durability_confirmed = false,
          .durability_failure = current_failure_message(),
      };
      {
         auto lock = std::scoped_lock{mutex_};
         mark_durability_uncertain_locked(durability_failure_message(result));
      }
      throw_durability_uncertain(result);
   }
   auto lock = std::scoped_lock{mutex_};
   mark_persistence_healthy_locked(true);
}

boost::asio::awaitable<void> dht::record_store::impl::async_close() {
   auto admission = admit_close();
   if (!admission) {
      co_return;
   }
   co_await wait_for_operations();
   auto ticket = co_await persistence_gate_.acquire();
   {
      auto lock = std::scoped_lock{mutex_};
      if (closed_) {
         co_return;
      }
   }
   try {
      co_await persistence_->async_flush();
   } catch (...) {
      auto result = dht::record_store::apply_result{
          .durability_confirmed = false,
          .durability_failure = current_failure_message(),
      };
      auto failure = std::exception_ptr{};
      try {
         throw_durability_uncertain(result);
      } catch (...) {
         failure = std::current_exception();
      }
      {
         auto lock = std::scoped_lock{mutex_};
         mark_durability_uncertain_locked(durability_failure_message(result));
      }
      std::rethrow_exception(failure);
   }
   {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_healthy_locked(true);
   }
   try {
      co_await persistence_->async_close();
   } catch (...) {
      auto failure = std::current_exception();
      {
         auto lock = std::scoped_lock{mutex_};
         mark_persistence_failure_locked(current_failure_message());
      }
      std::rethrow_exception(failure);
   }
   {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_healthy_locked(true);
      closed_ = true;
      closing_ = false;
   }
}

dht::record_store::persistence_status dht::record_store::impl::persistence_state() const {
   auto lock = std::scoped_lock{mutex_};
   return dht::record_store::persistence_status{
       .failure_count = persistence_failures_,
       .degraded = degraded_,
       .durability_uncertain = durability_uncertain_,
       .closing = closing_,
       .closed = closed_,
       .last_failure = last_failure_,
   };
}

} // namespace forge::net::p2p
