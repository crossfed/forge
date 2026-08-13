module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

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
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.net.p2p.peer_store;

import forge.asio.gate;
import forge.net.p2p.exceptions;
import forge.net.p2p.scoring;

#include "details/peer_store_impl.hxx"

namespace forge::net::p2p {
namespace {

[[nodiscard]] bool same_endpoint(const forge::net::p2p::endpoint& left, const forge::net::p2p::endpoint& right) {
   return left.to_string() == right.to_string();
}

[[nodiscard]] bool has_endpoint_source(const peer_store::endpoint_sources& sources) noexcept {
   return sources.learned || sources.identify_unsigned || sources.identify_signed;
}

void refresh_endpoint_score(peer_store::endpoint_record& endpoint) {
   endpoint.score = score_path(path::observation{
       .kind = endpoint.kind,
       .latency = endpoint.last_latency,
       .failures = endpoint.failures,
       .successes = endpoint.successes,
       .last_success = endpoint.successes > 0 && endpoint.failures == 0,
   });
}

void refresh_record_score(peer_store::record& record, path::kind kind, bool last_success) {
   record.score = score_path(path::observation{
       .kind = kind,
       .latency = record.last_latency,
       .failures = record.failures,
       .successes = record.successes,
       .last_success = last_success,
   });
}

void expire_reachability(peer_store::record& record, std::chrono::system_clock::time_point now) {
   if (record.reachability_expires_at == std::chrono::system_clock::time_point{} ||
       record.reachability_expires_at > now) {
      return;
   }
   record.reachability = reachability::state::unknown;
   record.observed_endpoint.reset();
   record.reachability_expires_at = {};
}

void normalize_for_storage(peer_store::record& value) {
   for (auto& endpoint : value.endpoints) {
      refresh_endpoint_score(endpoint);
   }
   const auto kind = value.endpoints.empty() ? path::kind::direct : value.endpoints.front().kind;
   refresh_record_score(value, kind, value.successes > 0);
}

void add_peer_record_bytes(std::size_t& total, std::size_t size, std::size_t maximum) {
   if (size > maximum - total) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P peer record exceeds byte limit");
   }
   total += size;
}

void validate_peer_record(const peer_store::record& value, const peer_store::options& options) {
   if (value.endpoints.size() > options.max_endpoints_per_peer ||
       value.protocols.size() > options.max_protocols_per_peer ||
       value.relay_reservations.size() > options.max_relay_reservations_per_peer) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P peer record exceeds collection limit");
   }

   auto bytes = std::size_t{};
   const auto add = [&](std::size_t size) { add_peer_record_bytes(bytes, size, options.max_peer_record_bytes); };
   add(value.protocol_version.size());
   add(value.agent_version.size());
   add(value.public_key.size());
   add(value.signed_peer_record.size());
   for (const auto& protocol : value.protocols) {
      add(protocol.value.size());
   }
   for (const auto& endpoint : value.endpoints) {
      if (!has_endpoint_source(endpoint.sources)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint record has no provenance");
      }
      add(endpoint.endpoint.to_string().size());
   }
   if (value.observed_endpoint) {
      add(value.observed_endpoint->to_string().size());
   }
   for (const auto& relay : value.relay_reservations) {
      if (relay.endpoints.size() > options.max_relay_endpoints_per_reservation) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P peer relay reservation exceeds endpoint limit");
      }
      add(relay.voucher.size());
      for (const auto& endpoint : relay.endpoints) {
         add(endpoint.to_string().size());
      }
   }
}

void validate_provider_record(const peer_store::provider_record& value, const peer_store::options& options) {
   if (value.provider.endpoints.size() > options.max_endpoints_per_peer) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P provider record exceeds endpoint limit");
   }

   auto bytes = std::size_t{};
   const auto add = [&](std::size_t size) { add_peer_record_bytes(bytes, size, options.max_peer_record_bytes); };
   add(value.key.bytes.size());
   add(value.provider.id.value.size());
   for (const auto& endpoint : value.provider.endpoints) {
      add(endpoint.to_string().size());
   }
}

void validate_rendezvous_record(const rendezvous::registration& value, const peer_store::options& options) {
   if (value.endpoints.size() > options.max_endpoints_per_peer) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P Rendezvous record exceeds endpoint limit");
   }

   auto bytes = std::size_t{};
   const auto add = [&](std::size_t size) { add_peer_record_bytes(bytes, size, options.max_peer_record_bytes); };
   add(value.namespace_name.size());
   add(value.peer.value.size());
   add(value.signed_peer_record.size());
   for (const auto& endpoint : value.endpoints) {
      add(endpoint.to_string().size());
   }
}

void mutate_endpoint(peer_store::record& record, const forge::net::p2p::endpoint& endpoint, path::kind kind,
                     const std::function<void(peer_store::endpoint_record&)>& mutation) {
   auto iterator = std::ranges::find_if(record.endpoints,
                                        [&](const auto& current) { return same_endpoint(current.endpoint, endpoint); });
   if (iterator == record.endpoints.end()) {
      iterator = record.endpoints.insert(record.endpoints.end(),
                                         peer_store::endpoint_record{.endpoint = endpoint, .kind = kind});
   }
   iterator->kind = kind;
   mutation(*iterator);
   refresh_endpoint_score(*iterator);
}

void replace_identify_endpoint_snapshot(peer_store::record& record,
                                        const std::vector<forge::net::p2p::endpoint>& endpoints,
                                        bool signed_snapshot) {
   for (auto& current : record.endpoints) {
      if (signed_snapshot) {
         current.sources.identify_signed = false;
      } else {
         current.sources.identify_unsigned = false;
      }
   }
   std::erase_if(record.endpoints, [](const auto& current) { return !has_endpoint_source(current.sources); });

   for (const auto& endpoint : endpoints) {
      const auto existing = std::ranges::find_if(
          record.endpoints, [&](const auto& current) { return same_endpoint(current.endpoint, endpoint); });
      if (existing == record.endpoints.end()) {
         auto sources = peer_store::endpoint_sources{.learned = false};
         sources.identify_signed = signed_snapshot;
         sources.identify_unsigned = !signed_snapshot;
         record.endpoints.push_back(peer_store::endpoint_record{
             .endpoint = endpoint,
             .kind = path::kind::direct,
             .sources = sources,
         });
      } else if (signed_snapshot) {
         existing->sources.identify_signed = true;
      } else {
         existing->sources.identify_unsigned = true;
      }
   }
}

[[nodiscard]] std::string current_failure_message() {
   try {
      throw;
   } catch (const std::exception& error) {
      return error.what();
   } catch (...) {
      return "unknown persistence failure";
   }
}

[[nodiscard]] std::string durability_failure_message(const peer_store::apply_result& result) {
   return result.durability_failure.empty() ? "persistence commit completed without durable acknowledgement"
                                            : result.durability_failure;
}

[[noreturn]] void throw_durability_uncertain(const peer_store::apply_result& result) {
   FORGE_THROW_EXCEPTION(exceptions::durability_uncertain, "peer state durability could not be confirmed",
                         forge::exceptions::ctx("reason", durability_failure_message(result)));
}

} // namespace

peer_store::impl::impl(peer_store::options options_value)
    : options_(std::move(options_value)), persistence_(options_.persistence) {
   if (options_.max_peers == 0 || options_.max_providers == 0 || options_.max_rendezvous == 0 ||
       options_.max_pending == 0 || options_.max_endpoints_per_peer == 0 || options_.max_protocols_per_peer == 0 ||
       options_.max_relay_reservations_per_peer == 0 || options_.max_relay_endpoints_per_reservation == 0 ||
       options_.max_peer_record_bytes == 0 || options_.hydration_page_limit == 0 || options_.prune_page_limit == 0 ||
       options_.max_persistence_waiters == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "peer store limits must be positive");
   }
   if (!persistence_) {
      persistence_ = peer_store::make_memory_persistence();
      options_.persistence = persistence_;
   }
}

peer_store::impl::persistence_admission::persistence_admission(peer_store::impl* owner) noexcept : owner_(owner) {}

peer_store::impl::persistence_admission::~persistence_admission() {
   if (owner_) {
      owner_->release_persistence_admission();
   }
}

peer_store::impl::persistence_admission::persistence_admission(persistence_admission&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)) {}

peer_store::impl::persistence_admission&
peer_store::impl::persistence_admission::operator=(persistence_admission&& other) noexcept {
   if (this == &other) {
      return *this;
   }
   if (owner_) {
      owner_->release_persistence_admission();
   }
   owner_ = std::exchange(other.owner_, nullptr);
   return *this;
}

peer_store::impl::close_admission::close_admission(peer_store::impl* owner) noexcept : owner_(owner) {}

peer_store::impl::close_admission::~close_admission() {
   if (owner_) {
      owner_->release_close_admission();
   }
}

peer_store::impl::close_admission::close_admission(close_admission&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)) {}

peer_store::impl::close_admission& peer_store::impl::close_admission::operator=(close_admission&& other) noexcept {
   if (this == &other) {
      return *this;
   }
   if (owner_) {
      owner_->release_close_admission();
   }
   owner_ = std::exchange(other.owner_, nullptr);
   return *this;
}

peer_store::impl::peer_mutation_stage::peer_mutation_stage(peer_store::impl* owner,
                                                           std::map<peer_id, peer_mutation> updates)
    : owner_(owner) {
   keys_.reserve(updates.size());
   for (const auto& [peer, _] : updates) {
      keys_.push_back(peer);
   }
   for (const auto& peer : keys_) {
      auto previous = owner_->pending_peer_mutations_.extract(peer);
      if (!previous.empty()) {
         previous_.insert(std::move(previous));
      }
   }
   while (!updates.empty()) {
      auto update = updates.extract(updates.begin());
      owner_->pending_peer_mutations_.insert(std::move(update));
   }
}

peer_store::impl::peer_mutation_stage::~peer_mutation_stage() {
   if (!owner_) {
      return;
   }
   for (const auto& peer : keys_) {
      static_cast<void>(owner_->pending_peer_mutations_.extract(peer));
   }
   while (!previous_.empty()) {
      auto previous = previous_.extract(previous_.begin());
      owner_->pending_peer_mutations_.insert(std::move(previous));
   }
}

void peer_store::impl::peer_mutation_stage::commit() noexcept {
   owner_ = nullptr;
}

void peer_store::impl::ensure_open_locked() const {
   if (closing_ || closed_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "peer store is closing or closed");
   }
}

std::size_t peer_store::impl::queued_unique_count_locked() const {
   auto count = in_flight_peer_mutations_.size();
   for (const auto& [peer, _] : pending_peer_mutations_) {
      if (!in_flight_peer_mutations_.contains(peer)) {
         ++count;
      }
   }
   return count;
}

void peer_store::impl::ensure_peer_mutation_capacity_locked(const std::vector<peer_id>& peers) const {
   auto additional = std::size_t{};
   auto unique = std::set<peer_id>{};
   for (const auto& peer : peers) {
      if (!unique.insert(peer).second || pending_peer_mutations_.contains(peer) ||
          in_flight_peer_mutations_.contains(peer)) {
         continue;
      }
      ++additional;
   }
   if (queued_unique_count_locked() + additional > options_.max_pending) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "peer store durable mutation queue is full");
   }
}

void peer_store::impl::add_peer_indexes(const peer_store::record& value) {
   const auto score = score_key{-value.score, value.peer};
   score_index_.insert(score);
   if (value.discovery_expires_at != std::chrono::system_clock::time_point{}) {
      peer_expiry_index_.emplace(value.discovery_expires_at, value.peer);
   }
   for (auto bit = std::uint64_t{1}; bit != 0; bit <<= 1U) {
      if (value.capabilities.has(bit)) {
         candidates_by_capability_[bit].insert(score);
      }
   }
}

void peer_store::impl::remove_peer_indexes(const peer_store::record& value) {
   const auto score = score_key{-value.score, value.peer};
   score_index_.erase(score);
   if (value.discovery_expires_at != std::chrono::system_clock::time_point{}) {
      peer_expiry_index_.erase({value.discovery_expires_at, value.peer});
   }
   for (auto bit = std::uint64_t{1}; bit != 0; bit <<= 1U) {
      if (!value.capabilities.has(bit)) {
         continue;
      }
      const auto index = candidates_by_capability_.find(bit);
      if (index == candidates_by_capability_.end()) {
         continue;
      }
      index->second.erase(score);
      if (index->second.empty()) {
         candidates_by_capability_.erase(index);
      }
   }
}

void peer_store::impl::erase_peer_operational(const peer_id& peer) {
   const auto current = records_.find(peer);
   if (current == records_.end()) {
      return;
   }
   remove_peer_indexes(current->second);
   records_.erase(current);
}

std::optional<peer_id> peer_store::impl::store_peer_operational(peer_store::record value) {
   const auto peer = value.peer;
   const auto current = records_.find(value.peer);
   if (current != records_.end()) {
      remove_peer_indexes(current->second);
   }
   records_.insert_or_assign(peer, std::move(value));
   add_peer_indexes(records_.at(peer));

   if (records_.size() <= options_.max_peers) {
      return std::nullopt;
   }
   const auto lowest_score = score_index_.rbegin();
   auto evicted = lowest_score->second;
   erase_peer_operational(evicted);
   return evicted;
}

peer_store::record peer_store::impl::record_for_mutation_locked(const peer_id& peer) const {
   const auto current = records_.find(peer);
   if (current != records_.end()) {
      return current->second;
   }
   return peer_store::record{.peer = peer};
}

void peer_store::impl::commit_peer_mutation(peer_store::record value) {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   commit_peer_mutation_locked(std::move(value));
}

void peer_store::impl::commit_peer_mutation_locked(peer_store::record value) {
   validate_peer_record(value, options_);
   auto evicted = std::optional<peer_id>{};
   if (!records_.contains(value.peer) && records_.size() == options_.max_peers) {
      const auto candidate = score_key{-value.score, value.peer};
      const auto current_lowest = *score_index_.rbegin();
      evicted = candidate > current_lowest ? value.peer : current_lowest.second;
   }

   auto affected = std::vector<peer_id>{value.peer};
   if (evicted) {
      affected.push_back(*evicted);
   }
   ensure_peer_mutation_capacity_locked(affected);

   const auto peer = value.peer;
   auto updates = std::map<peer_id, peer_mutation>{};
   if (!evicted || *evicted != peer) {
      updates.emplace(peer, peer_mutation{.value = value});
   }
   if (evicted) {
      updates.insert_or_assign(*evicted, peer_mutation{.value = std::nullopt});
   }
   auto stage = peer_mutation_stage{this, std::move(updates)};
   static_cast<void>(store_peer_operational(std::move(value)));
   stage.commit();
}

peer_store::record
peer_store::impl::mutate_peer(const peer_id& peer, const std::function<void(peer_store::record&)>& mutation) {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   auto value = record_for_mutation_locked(peer);
   mutation(value);
   value.peer = peer;
   normalize_for_storage(value);
   auto result = value;
   commit_peer_mutation_locked(std::move(value));
   return result;
}

void peer_store::impl::upsert(peer_store::record value) {
   normalize_for_storage(value);
   commit_peer_mutation(std::move(value));
}

peer_store::record peer_store::impl::apply_identify(const peer_id& peer, peer_store::identify_update update) {
   if (update.signed_endpoints.has_value() != update.signed_peer_record.has_value() ||
       (update.signed_endpoints && update.unsigned_endpoints)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P Identify update has inconsistent endpoint sources");
   }

   return mutate_peer(peer, [&](peer_store::record& value) {
      if (update.protocol_version) {
         value.protocol_version = std::move(*update.protocol_version);
      }
      if (update.agent_version) {
         value.agent_version = std::move(*update.agent_version);
      }
      if (update.public_key) {
         value.public_key = std::move(*update.public_key);
      }
      if (update.protocols) {
         value.protocols = std::move(*update.protocols);
      }
      if (update.capabilities) {
         value.capabilities = *update.capabilities;
      }
      if (update.replace_observed_endpoint) {
         value.observed_endpoint = std::move(update.observed_endpoint);
      }
      if (update.signed_endpoints) {
         value.signed_peer_record = std::move(*update.signed_peer_record);
         replace_identify_endpoint_snapshot(value, *update.signed_endpoints, true);
      } else if (update.unsigned_endpoints) {
         replace_identify_endpoint_snapshot(value, *update.unsigned_endpoints, false);
      }
   });
}

std::optional<peer_store::record> peer_store::impl::apply_discovery(const peer_id& peer,
                                                                    peer_store::discovery_update update) {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   const auto current = records_.find(peer);
   if (current == records_.end()) {
      return std::nullopt;
   }
   auto value = current->second;
   value.discovered_by = update.source;
   value.discovered_at = update.observed_at;
   value.discovery_expires_at = update.expires_at;
   normalize_for_storage(value);
   auto result = value;
   commit_peer_mutation_locked(std::move(value));
   return result;
}

void peer_store::impl::apply_peer_exchange(const peer_id& peer, capability_set capabilities) {
   static_cast<void>(mutate_peer(peer, [&](peer_store::record& value) {
      value.capabilities.bits |= capabilities.bits;
   }));
}

void peer_store::impl::upsert_relay_reservation(peer_store::relay_record value) {
   const auto peer = value.relay;
   static_cast<void>(mutate_peer(peer, [&](peer_store::record& record) {
      const auto current = std::ranges::find_if(
          record.relay_reservations, [&](const auto& reservation) { return reservation.relay == peer; });
      if (current == record.relay_reservations.end()) {
         record.relay_reservations.push_back(std::move(value));
      } else {
         *current = std::move(value);
      }
      record.capabilities.add(capabilities::relay);
      record.capabilities.add(capabilities::relay_reservation);
   }));
}

bool peer_store::impl::mark_discovery_failure(const peer_id& peer,
                                              std::chrono::system_clock::time_point backoff_until) {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   const auto current = records_.find(peer);
   if (current == records_.end()) {
      return false;
   }
   auto value = current->second;
   value.discovery_backoff_until = backoff_until;
   ++value.failures;
   normalize_for_storage(value);
   commit_peer_mutation_locked(std::move(value));
   return true;
}

std::size_t peer_store::impl::prune_expired_relay_reservations(
   const peer_id& peer, std::chrono::system_clock::time_point now) {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   const auto current = records_.find(peer);
   if (current == records_.end()) {
      return 0;
   }
   auto value = current->second;
   const auto before = value.relay_reservations.size();
   std::erase_if(value.relay_reservations, [&](const peer_store::relay_record& reservation) {
      return reservation.expires_at != std::chrono::system_clock::time_point{} && reservation.expires_at <= now;
   });
   const auto removed = before - value.relay_reservations.size();
   if (removed == 0) {
      return 0;
   }
   normalize_for_storage(value);
   commit_peer_mutation_locked(std::move(value));
   return removed;
}

void peer_store::impl::learn_endpoint(peer_id peer, forge::net::p2p::endpoint endpoint, capability_set capabilities) {
   static_cast<void>(mutate_peer(peer, [&](peer_store::record& value) {
      value.peer = peer;
      value.capabilities.bits |= capabilities.bits;
      const auto existing = std::ranges::find_if(
          value.endpoints, [&](const auto& current) { return same_endpoint(current.endpoint, endpoint); });
      if (existing == value.endpoints.end()) {
         value.endpoints.push_back(peer_store::endpoint_record{.endpoint = std::move(endpoint)});
      } else {
         existing->sources.learned = true;
      }
   }));
}

void peer_store::impl::mark_reachability(peer_id peer, reachability::state state,
                                         std::optional<forge::net::p2p::endpoint> observed) {
   static_cast<void>(mutate_peer(peer, [&](peer_store::record& value) {
      value.peer = peer;
      value.reachability = state;
      value.observed_endpoint = std::move(observed);
      value.reachability_expires_at = std::chrono::system_clock::now() + std::chrono::minutes{5};
   }));
}

void peer_store::impl::mark_success(const peer_id& peer, path::kind kind, std::chrono::milliseconds latency) {
   static_cast<void>(mutate_peer(peer, [&](peer_store::record& value) {
      ++value.successes;
      value.last_latency = latency;
      refresh_record_score(value, kind, true);
   }));
}

void peer_store::impl::mark_failure(const peer_id& peer) {
   static_cast<void>(mutate_peer(peer, [&](peer_store::record& value) {
      ++value.failures;
      const auto kind = value.endpoints.empty() ? path::kind::direct : value.endpoints.front().kind;
      refresh_record_score(value, kind, false);
   }));
}

void peer_store::impl::mark_endpoint_success(const peer_id& peer, const forge::net::p2p::endpoint& endpoint,
                                             path::kind kind, std::chrono::milliseconds latency) {
   static_cast<void>(mutate_peer(peer, [&](peer_store::record& value) {
      mutate_endpoint(value, endpoint, kind, [&](peer_store::endpoint_record& current) {
         current.sources.learned = true;
         current.last_latency = latency;
         current.backoff_until = {};
         ++current.successes;
      });
      ++value.successes;
      value.last_latency = latency;
      refresh_record_score(value, kind, true);
   }));
}

void peer_store::impl::mark_endpoint_failure(const peer_id& peer, const forge::net::p2p::endpoint& endpoint,
                                             path::kind kind, std::chrono::system_clock::time_point backoff_until) {
   static_cast<void>(mutate_peer(peer, [&](peer_store::record& value) {
      mutate_endpoint(value, endpoint, kind, [&](peer_store::endpoint_record& current) {
         current.backoff_until = backoff_until;
         ++current.failures;
      });
      ++value.failures;
      refresh_record_score(value, kind, false);
   }));
}

void peer_store::impl::upsert_routing_peer(dht::peer value, discovery::source source,
                                           std::chrono::system_clock::time_point expires_at) {
   const auto peer = value.id;
   static_cast<void>(mutate_peer(peer, [&](peer_store::record& record) {
      record.peer = peer;
      record.capabilities.add(capabilities::dht);
      record.discovered_by = source;
      record.discovered_at = std::chrono::system_clock::now();
      record.discovery_expires_at = expires_at;
      for (auto endpoint : value.endpoints) {
         endpoint.peer = peer;
         const auto existing = std::ranges::find_if(
             record.endpoints, [&](const auto& current) { return same_endpoint(current.endpoint, endpoint); });
         if (existing == record.endpoints.end()) {
            record.endpoints.push_back(peer_store::endpoint_record{.endpoint = std::move(endpoint)});
         } else {
            existing->sources.learned = true;
         }
      }
   }));
}

void peer_store::impl::mark_persistence_failure_locked(std::string message) {
   degraded_ = true;
   ++persistence_failures_;
   last_failure_ = std::move(message);
}

void peer_store::impl::mark_durability_uncertain_locked(std::string message) {
   durability_uncertain_ = true;
   mark_persistence_failure_locked(std::move(message));
}

void peer_store::impl::mark_persistence_healthy_locked(bool durability_confirmed) {
   if (durability_confirmed) {
      durability_uncertain_ = false;
   }
   if (durability_uncertain_) {
      return;
   }
   degraded_ = false;
   last_failure_.clear();
}

std::pair<peer_store::mutation_batch, std::map<peer_id, peer_store::impl::peer_mutation>>
peer_store::impl::take_pending_batch_locked() {
   auto batch = peer_store::mutation_batch{};
   auto mutations = std::move(pending_peer_mutations_);
   pending_peer_mutations_.clear();
   batch.peer_upserts.reserve(mutations.size());
   batch.peer_removals.reserve(mutations.size());
   for (const auto& [peer, mutation] : mutations) {
      if (mutation.value) {
         batch.peer_upserts.push_back(*mutation.value);
      } else {
         batch.peer_removals.push_back(peer);
      }
   }
   in_flight_peer_mutations_ = mutations;
   return {std::move(batch), std::move(mutations)};
}

void peer_store::impl::complete_peer_mutations_locked(const std::map<peer_id, peer_mutation>& values) {
   for (const auto& [peer, _] : values) {
      in_flight_peer_mutations_.erase(peer);
   }
}

void peer_store::impl::requeue_peer_mutations_locked(const std::map<peer_id, peer_mutation>& values) {
   for (const auto& [peer, mutation] : values) {
      if (!pending_peer_mutations_.contains(peer)) {
         pending_peer_mutations_.emplace(peer, mutation);
      }
      in_flight_peer_mutations_.erase(peer);
   }
}

void peer_store::impl::store_rendezvous_operational(rendezvous::registration value) {
   const auto key = rendezvous_map_key{value.namespace_name, value.peer};
   erase_rendezvous_operational(key);
   rendezvous_by_sequence_.emplace(rendezvous_sequence_key{value.namespace_name, value.sequence, value.peer}, key);
   rendezvous_by_global_sequence_.emplace(rendezvous_global_sequence_key{value.sequence, key}, key);
   rendezvous_expiry_index_.emplace(value.expires_at, key);
   ++rendezvous_per_peer_[value.peer];
   rendezvous_.emplace(key, std::move(value));
}

void peer_store::impl::erase_rendezvous_operational(const rendezvous_map_key& key) {
   const auto current = rendezvous_.find(key);
   if (current == rendezvous_.end()) {
      return;
   }
   rendezvous_by_sequence_.erase(
       rendezvous_sequence_key{current->second.namespace_name, current->second.sequence, current->second.peer});
   rendezvous_by_global_sequence_.erase(rendezvous_global_sequence_key{current->second.sequence, key});
   rendezvous_expiry_index_.erase({current->second.expires_at, key});
   const auto count = rendezvous_per_peer_.find(current->second.peer);
   if (count != rendezvous_per_peer_.end() && --count->second == 0) {
      rendezvous_per_peer_.erase(count);
   }
   rendezvous_.erase(current);
}

boost::asio::awaitable<void> peer_store::impl::apply_pending_locked_gate(bool flush_backend) {
   auto batch = peer_store::mutation_batch{};
   auto mutations = std::map<peer_id, peer_mutation>{};
   {
      auto lock = std::scoped_lock{mutex_};
      std::tie(batch, mutations) = take_pending_batch_locked();
   }

   if (!mutations.empty()) {
      auto result = peer_store::apply_result{};
      try {
         result = co_await persistence_->async_apply(batch);
      } catch (...) {
         auto lock = std::scoped_lock{mutex_};
         requeue_peer_mutations_locked(mutations);
         mark_persistence_failure_locked(current_failure_message());
         throw;
      }
      {
         auto lock = std::scoped_lock{mutex_};
         complete_peer_mutations_locked(mutations);
         if (result.durability_confirmed) {
            mark_persistence_healthy_locked(batch.durable);
         } else {
            mark_durability_uncertain_locked(durability_failure_message(result));
         }
      }
      if (!result.durability_confirmed) {
         throw_durability_uncertain(result);
      }
   }

   if (!flush_backend) {
      co_return;
   }
   try {
      co_await persistence_->async_flush();
   } catch (...) {
      auto result = peer_store::apply_result{
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

peer_store::impl::persistence_admission peer_store::impl::admit_persistence_operation() {
   auto lock = std::scoped_lock{mutex_};
   ensure_open_locked();
   if (persistence_admissions_ >= options_.max_persistence_waiters) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "peer store persistence waiter limit reached");
   }
   ++persistence_admissions_;
   return persistence_admission{this};
}

void peer_store::impl::release_persistence_admission() noexcept {
   auto drainers = std::map<const void*, std::function<void()>>{};
   {
      auto lock = std::scoped_lock{mutex_};
      if (persistence_admissions_ > 0) {
         --persistence_admissions_;
      }
      if (persistence_admissions_ == 0) {
         drainers.swap(persistence_admission_drainers_);
      }
   }
   for (const auto& [_, drain] : drainers) {
      drain();
   }
}

boost::asio::awaitable<void> peer_store::impl::wait_for_persistence_admissions() {
   while (true) {
      auto timer = std::make_shared<boost::asio::steady_timer>(co_await boost::asio::this_coro::executor);
      timer->expires_at(std::chrono::steady_clock::time_point::max());
      const auto* drainer_id = timer.get();
      {
         auto lock = std::scoped_lock{mutex_};
         if (persistence_admissions_ == 0) {
            co_return;
         }
         persistence_admission_drainers_.emplace(drainer_id, [weak = std::weak_ptr{timer}] {
            if (auto current = weak.lock()) {
               boost::asio::post(current->get_executor(), [current] { current->cancel(); });
            }
         });
      }
      auto error = boost::system::error_code{};
      co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      auto drained = false;
      {
         auto lock = std::scoped_lock{mutex_};
         persistence_admission_drainers_.erase(drainer_id);
         drained = persistence_admissions_ == 0;
      }
      if (drained) {
         co_return;
      }
      if (error == boost::asio::error::operation_aborted) {
         FORGE_THROW_EXCEPTION(exceptions::canceled, "peer store close was canceled while draining operations");
      }
      if (error) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "peer store close admission wait failed",
                               forge::exceptions::ctx("error", error.message()));
      }
   }
}

std::optional<peer_store::impl::close_admission> peer_store::impl::admit_close_operation() {
   auto lock = std::scoped_lock{mutex_};
   if (closed_) {
      return std::nullopt;
   }
   if (close_waiters_ >= options_.max_persistence_waiters) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "peer store close waiter limit reached");
   }
   closing_ = true;
   ++close_waiters_;
   return close_admission{this};
}

void peer_store::impl::release_close_admission() noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (close_waiters_ > 0) {
      --close_waiters_;
   }
}

boost::asio::awaitable<void> peer_store::impl::async_upsert_provider(peer_store::provider_record value) {
   auto admission = admit_persistence_operation();
   auto ticket = co_await persistence_gate_.acquire();
   validate_provider_record(value, options_);
   const auto key = provider_key{value.key.bytes, value.provider.id};
   {
      auto lock = std::scoped_lock{mutex_};
      if (!providers_.contains(key) && providers_.size() >= options_.max_providers) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "peer store provider capacity reached");
      }
   }

   auto batch = peer_store::mutation_batch{};
   batch.provider_upserts.push_back(value);
   batch.durable = true;
   auto result = peer_store::apply_result{};
   try {
      result = co_await persistence_->async_apply(std::move(batch));
   } catch (...) {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked(current_failure_message());
      throw;
   }

   {
      auto lock = std::scoped_lock{mutex_};
      const auto current = providers_.find(key);
      if (current != providers_.end()) {
         provider_expiry_index_.erase(
             {current->second.expires_at, current->second.key.bytes, current->second.provider.id});
      }
      auto [stored, _] = providers_.insert_or_assign(key, std::move(value));
      if (stored->second.expires_at != std::chrono::system_clock::time_point{}) {
         provider_expiry_index_.emplace(stored->second.expires_at, stored->second.key.bytes,
                                        stored->second.provider.id);
      }
      if (result.durability_confirmed) {
         mark_persistence_healthy_locked(true);
      } else {
         mark_durability_uncertain_locked(durability_failure_message(result));
      }
   }
   if (!result.durability_confirmed) {
      throw_durability_uncertain(result);
   }
}

boost::asio::awaitable<void> peer_store::impl::async_upsert_rendezvous(rendezvous::registration value) {
   co_await async_store_rendezvous(std::move(value), std::nullopt);
}

boost::asio::awaitable<void> peer_store::impl::async_register_rendezvous(rendezvous::registration value,
                                                                         std::size_t max_registrations_per_peer) {
   if (max_registrations_per_peer == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "peer store Rendezvous per-peer capacity must be positive");
   }
   co_await async_store_rendezvous(std::move(value), max_registrations_per_peer);
}

boost::asio::awaitable<void>
peer_store::impl::async_store_rendezvous(rendezvous::registration value,
                                         std::optional<std::size_t> max_registrations_per_peer) {
   auto admission = admit_persistence_operation();
   auto ticket = co_await persistence_gate_.acquire();
   validate_rendezvous_record(value, options_);
   const auto key = rendezvous_map_key{value.namespace_name, value.peer};
   {
      auto lock = std::scoped_lock{mutex_};
      if (!rendezvous_.contains(key) && rendezvous_.size() >= options_.max_rendezvous) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "peer store Rendezvous capacity reached");
      }
      const auto existing = rendezvous_.contains(key);
      const auto count = rendezvous_per_peer_.find(value.peer);
      const auto registrations = count == rendezvous_per_peer_.end() ? 0U : count->second;
      if (!existing && max_registrations_per_peer && registrations >= *max_registrations_per_peer) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "peer store Rendezvous per-peer capacity reached");
      }
      if (rendezvous_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
         FORGE_THROW_EXCEPTION(exceptions::sequence_exhausted, "peer store Rendezvous sequence is exhausted");
      }
      value.sequence = ++rendezvous_sequence_;
   }

   auto batch = peer_store::mutation_batch{};
   batch.rendezvous_upserts.push_back(value);
   batch.rendezvous_sequence_high_watermark = value.sequence;
   batch.durable = true;
   auto result = peer_store::apply_result{};
   try {
      result = co_await persistence_->async_apply(std::move(batch));
   } catch (...) {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked(current_failure_message());
      throw;
   }

   {
      auto lock = std::scoped_lock{mutex_};
      store_rendezvous_operational(std::move(value));
      if (result.durability_confirmed) {
         mark_persistence_healthy_locked(true);
      } else {
         mark_durability_uncertain_locked(durability_failure_message(result));
      }
   }
   if (!result.durability_confirmed) {
      throw_durability_uncertain(result);
   }
}

boost::asio::awaitable<void> peer_store::impl::async_remove_rendezvous(peer_id peer, std::string namespace_name) {
   auto admission = admit_persistence_operation();
   auto ticket = co_await persistence_gate_.acquire();
   validate_rendezvous_record(rendezvous::registration{.namespace_name = namespace_name, .peer = peer}, options_);

   auto batch = peer_store::mutation_batch{};
   batch.rendezvous_removals.push_back(peer_store::rendezvous_key{.namespace_name = namespace_name, .peer = peer});
   batch.durable = true;
   auto result = peer_store::apply_result{};
   try {
      result = co_await persistence_->async_apply(std::move(batch));
   } catch (...) {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked(current_failure_message());
      throw;
   }

   {
      auto lock = std::scoped_lock{mutex_};
      erase_rendezvous_operational({std::move(namespace_name), std::move(peer)});
      if (result.durability_confirmed) {
         mark_persistence_healthy_locked(true);
      } else {
         mark_durability_uncertain_locked(durability_failure_message(result));
      }
   }
   if (!result.durability_confirmed) {
      throw_durability_uncertain(result);
   }
}

void peer_store::impl::hydrate_page_locked(peer_store::hydration_page page) {
   for (const auto& value : page.peers) {
      validate_peer_record(value, options_);
   }
   for (const auto& value : page.providers) {
      validate_provider_record(value, options_);
   }
   for (const auto& value : page.rendezvous_registrations) {
      validate_rendezvous_record(value, options_);
   }
   rendezvous_sequence_ = std::max(rendezvous_sequence_, page.rendezvous_sequence_high_watermark);
   auto removals = std::map<peer_id, peer_mutation>{};
   auto removal_peers = std::vector<peer_id>{};
   for (const auto& value : page.peers) {
      if (!pending_peer_mutations_.contains(value.peer) && !in_flight_peer_mutations_.contains(value.peer) &&
          !records_.contains(value.peer) && records_.size() >= options_.max_peers) {
         removals.emplace(value.peer, peer_mutation{.value = std::nullopt});
         removal_peers.push_back(value.peer);
      }
   }
   ensure_peer_mutation_capacity_locked(removal_peers);
   auto removal_stage = peer_mutation_stage{this, std::move(removals)};
   for (auto& value : page.peers) {
      if (pending_peer_mutations_.contains(value.peer) || in_flight_peer_mutations_.contains(value.peer)) {
         continue;
      }
      if (!records_.contains(value.peer) && records_.size() >= options_.max_peers) {
         continue;
      }
      normalize_for_storage(value);
      (void)store_peer_operational(std::move(value));
   }
   for (auto& value : page.providers) {
      const auto key = provider_key{value.key.bytes, value.provider.id};
      if (providers_.contains(key) || providers_.size() < options_.max_providers) {
         const auto current = providers_.find(key);
         if (current != providers_.end()) {
            provider_expiry_index_.erase(
                {current->second.expires_at, current->second.key.bytes, current->second.provider.id});
         }
         auto [stored, _] = providers_.insert_or_assign(key, std::move(value));
         if (stored->second.expires_at != std::chrono::system_clock::time_point{}) {
            provider_expiry_index_.emplace(stored->second.expires_at, stored->second.key.bytes,
                                           stored->second.provider.id);
         }
      }
   }
   for (auto& value : page.rendezvous_registrations) {
      rendezvous_sequence_ = std::max(rendezvous_sequence_, value.sequence);
      const auto key = rendezvous_map_key{value.namespace_name, value.peer};
      if (rendezvous_.contains(key) || rendezvous_.size() < options_.max_rendezvous) {
         store_rendezvous_operational(std::move(value));
      }
   }
   removal_stage.commit();
}

boost::asio::awaitable<void> peer_store::impl::async_hydrate() {
   auto admission = admit_persistence_operation();
   auto ticket = co_await persistence_gate_.acquire();

   const auto hydrate_kind = [this](peer_store::hydration_kind kind,
                                    std::size_t maximum) -> boost::asio::awaitable<void> {
      auto cursor = std::optional<std::vector<std::byte>>{};
      auto remaining = maximum;
      while (remaining > 0) {
         const auto limit = std::min(remaining, options_.hydration_page_limit);
         auto page = peer_store::hydration_page{};
         try {
            page = co_await persistence_->async_hydrate(
                peer_store::hydration_request{.kind = kind, .cursor = cursor, .limit = limit});
         } catch (...) {
            auto lock = std::scoped_lock{mutex_};
            mark_persistence_failure_locked(current_failure_message());
            throw;
         }

         const auto page_size = page.peers.size() + page.providers.size() + page.rendezvous_registrations.size();
         const auto wrong_kind =
             (kind != peer_store::hydration_kind::peers && !page.peers.empty()) ||
             (kind != peer_store::hydration_kind::providers && !page.providers.empty()) ||
             (kind != peer_store::hydration_kind::rendezvous && !page.rendezvous_registrations.empty());
         if (page_size > limit || wrong_kind || (page.cursor && (page.cursor == cursor || page_size == 0))) {
            auto lock = std::scoped_lock{mutex_};
            mark_persistence_failure_locked("persistence returned an invalid hydration page");
            FORGE_THROW_EXCEPTION(exceptions::internal, "persistence returned an invalid hydration page");
         }

         const auto next_cursor = page.cursor;
         try {
            auto lock = std::scoped_lock{mutex_};
            hydrate_page_locked(std::move(page));
         } catch (...) {
            auto lock = std::scoped_lock{mutex_};
            mark_persistence_failure_locked(current_failure_message());
            throw;
         }
         remaining -= page_size;
         if (!next_cursor) {
            co_return;
         }
         cursor = next_cursor;
      }
   };

   co_await hydrate_kind(peer_store::hydration_kind::peers, options_.max_peers);
   co_await hydrate_kind(peer_store::hydration_kind::providers, options_.max_providers);
   co_await hydrate_kind(peer_store::hydration_kind::rendezvous, options_.max_rendezvous);

   auto lock = std::scoped_lock{mutex_};
   mark_persistence_healthy_locked();
}

void peer_store::impl::prune_operational_locked(std::chrono::system_clock::time_point,
                                                const peer_store::prune_result& result) {
   for (const auto& peer : result.peers) {
      if (pending_peer_mutations_.contains(peer) || in_flight_peer_mutations_.contains(peer)) {
         continue;
      }
      erase_peer_operational(peer);
   }
   for (const auto& value : result.providers) {
      const auto key = provider_key{value.key.bytes, value.provider.id};
      if (const auto current = providers_.find(key); current != providers_.end()) {
         provider_expiry_index_.erase(
             {current->second.expires_at, current->second.key.bytes, current->second.provider.id});
         providers_.erase(current);
      }
   }
   for (const auto& value : result.rendezvous_registrations) {
      const auto key = rendezvous_map_key{value.namespace_name, value.peer};
      erase_rendezvous_operational(key);
   }
}

boost::asio::awaitable<peer_store::prune_result>
peer_store::impl::async_prune_expired(std::chrono::system_clock::time_point now) {
   auto admission = admit_persistence_operation();
   auto ticket = co_await persistence_gate_.acquire();

   co_await apply_pending_locked_gate(false);

   auto result = peer_store::prune_result{};
   try {
      result = co_await persistence_->async_prune_expired(now, options_.prune_page_limit);
   } catch (...) {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked(current_failure_message());
      throw;
   }

   auto lock = std::scoped_lock{mutex_};
   prune_operational_locked(now, result);
   mark_persistence_healthy_locked();
   co_return result;
}

boost::asio::awaitable<void> peer_store::impl::async_flush() {
   auto admission = admit_persistence_operation();
   auto ticket = co_await persistence_gate_.acquire();
   co_await apply_pending_locked_gate(true);
}

boost::asio::awaitable<void> peer_store::impl::async_close() {
   auto admission = admit_close_operation();
   if (!admission) {
      co_return;
   }

   co_await wait_for_persistence_admissions();

   auto ticket = co_await persistence_gate_.acquire();

   {
      auto lock = std::scoped_lock{mutex_};
      if (closed_) {
         co_return;
      }
      closing_ = true;
   }

   try {
      while (true) {
         co_await apply_pending_locked_gate(false);
         auto lock = std::scoped_lock{mutex_};
         if (pending_peer_mutations_.empty()) {
            break;
         }
      }
   } catch (...) {
      throw;
   }

   try {
      co_await persistence_->async_flush();
   } catch (...) {
      auto result = peer_store::apply_result{
          .durability_confirmed = false,
          .durability_failure = current_failure_message(),
      };
      {
         auto lock = std::scoped_lock{mutex_};
         mark_durability_uncertain_locked(durability_failure_message(result));
      }
      throw_durability_uncertain(result);
   }
   {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_healthy_locked(true);
   }
   try {
      co_await persistence_->async_close();
   } catch (...) {
      auto lock = std::scoped_lock{mutex_};
      mark_persistence_failure_locked(current_failure_message());
      throw;
   }

   auto lock = std::scoped_lock{mutex_};
   mark_persistence_healthy_locked(true);
   closing_ = false;
   closed_ = true;
}

std::optional<peer_store::record> peer_store::impl::find(const peer_id& peer) const {
   auto lock = std::scoped_lock{mutex_};
   const auto iterator = records_.find(peer);
   if (iterator == records_.end()) {
      return std::nullopt;
   }
   auto value = iterator->second;
   expire_reachability(value, std::chrono::system_clock::now());
   return value;
}

std::vector<peer_store::record> peer_store::impl::snapshot(std::size_t limit) const {
   auto lock = std::scoped_lock{mutex_};
   auto result = std::vector<peer_store::record>{};
   result.reserve(std::min(limit, records_.size()));
   const auto now = std::chrono::system_clock::now();
   for (const auto& [_, value] : records_) {
      if (result.size() == limit) {
         break;
      }
      auto copy = value;
      expire_reachability(copy, now);
      result.push_back(std::move(copy));
   }
   return result;
}

std::vector<peer_store::record> peer_store::impl::candidates(std::uint64_t capability, std::size_t limit) const {
   auto lock = std::scoped_lock{mutex_};
   auto result = std::vector<peer_store::record>{};
   if (capability == 0 || limit == 0) {
      return result;
   }

   const auto index_capability = capability & (~capability + 1U);
   const auto index = candidates_by_capability_.find(index_capability);
   if (index == candidates_by_capability_.end()) {
      return result;
   }

   result.reserve(std::min(limit, index->second.size()));
   const auto now = std::chrono::system_clock::now();
   for (const auto& [_, peer] : index->second) {
      if (result.size() == limit) {
         break;
      }
      const auto record = records_.find(peer);
      if (record == records_.end() || !record->second.capabilities.has(capability)) {
         continue;
      }
      if (record->second.discovery_expires_at != std::chrono::system_clock::time_point{} &&
          record->second.discovery_expires_at <= now) {
         continue;
      }
      auto copy = record->second;
      expire_reachability(copy, now);
      result.push_back(std::move(copy));
   }
   return result;
}

std::vector<peer_store::provider_record> peer_store::impl::find_providers(const dht::key& key,
                                                                          std::size_t limit) const {
   auto lock = std::scoped_lock{mutex_};
   auto result = std::vector<peer_store::provider_record>{};
   const auto now = std::chrono::system_clock::now();
   for (auto iterator = providers_.lower_bound({key.bytes, peer_id{}});
        iterator != providers_.end() && iterator->first.first == key.bytes; ++iterator) {
      if (result.size() == limit) {
         break;
      }
      if (iterator->second.expires_at == std::chrono::system_clock::time_point{} || iterator->second.expires_at > now) {
         result.push_back(iterator->second);
      }
   }
   return result;
}

std::vector<rendezvous::registration> peer_store::impl::discover_rendezvous(std::string_view namespace_name,
                                                                            std::uint64_t after_sequence,
                                                                            std::size_t limit) const {
   auto lock = std::scoped_lock{mutex_};
   auto result = std::vector<rendezvous::registration>{};
   const auto now = std::chrono::system_clock::now();
   if (namespace_name.empty()) {
      for (auto iterator = rendezvous_by_global_sequence_.lower_bound({after_sequence, {}});
           iterator != rendezvous_by_global_sequence_.end() && result.size() < limit; ++iterator) {
         if (iterator->first.first <= after_sequence) {
            continue;
         }
         const auto value = rendezvous_.find(iterator->second);
         if (value != rendezvous_.end() && value->second.expires_at > now) {
            result.push_back(value->second);
         }
      }
      return result;
   }

   for (auto iterator = rendezvous_by_sequence_.lower_bound(
            rendezvous_sequence_key{std::string{namespace_name}, after_sequence, peer_id{}});
        iterator != rendezvous_by_sequence_.end() && result.size() < limit; ++iterator) {
      const auto& [current_namespace, sequence, _] = iterator->first;
      if (current_namespace != namespace_name) {
         break;
      }
      if (sequence <= after_sequence) {
         continue;
      }
      const auto value = rendezvous_.find(iterator->second);
      if (value != rendezvous_.end() && value->second.expires_at > now) {
         result.push_back(value->second);
      }
   }
   return result;
}

peer_store::persistence_status peer_store::impl::persistence_state() const {
   auto lock = std::scoped_lock{mutex_};
   return peer_store::persistence_status{
       .pending_peer_mutations = queued_unique_count_locked(),
       .failure_count = persistence_failures_,
       .degraded = degraded_,
       .closing = closing_,
       .closed = closed_,
       .last_failure = last_failure_,
   };
}

} // namespace forge::net::p2p
