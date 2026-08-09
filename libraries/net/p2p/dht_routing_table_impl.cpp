module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

module forge.net.p2p.dht;

import forge.net.p2p.exceptions;

#include "details/dht_routing_table_impl.hxx"

namespace forge::net::p2p {
namespace {

template <typename Entries> [[nodiscard]] auto find_entry(Entries& entries, const peer_id& peer) {
   return std::ranges::find_if(entries, [&](const auto& value) { return value.value.id == peer; });
}

[[nodiscard]] std::size_t common_prefix_bits(const dht::distance& distance) noexcept {
   auto result = std::size_t{};
   for (const auto byte : distance.bytes) {
      if (byte == 0) {
         result += 8;
         continue;
      }
      result += static_cast<std::size_t>(std::countl_zero(byte));
      break;
   }
   return std::min(result, std::size_t{255});
}

template <typename Bucket, typename Entry> void insert_replacement(Bucket& bucket, Entry value, std::size_t capacity) {
   if (auto current = find_entry(bucket.replacements, value.value.id); current != bucket.replacements.end()) {
      if (current->admission == dht::routing_admission::verified_server &&
          value.admission == dht::routing_admission::candidate) {
         return;
      }
      if (value.admission == dht::routing_admission::candidate) {
         value.failures = current->failures;
      }
      *current = std::move(value);
   } else {
      bucket.replacements.push_back(std::move(value));
   }
   std::ranges::sort(bucket.replacements, [](const auto& left, const auto& right) {
      if (left.admission != right.admission) {
         return left.admission == dht::routing_admission::verified_server;
      }
      return left.sequence > right.sequence;
   });
   if (bucket.replacements.size() > capacity) {
      bucket.replacements.resize(capacity);
   }
}

template <typename Bucket> void promote_replacement(Bucket& bucket) {
   const auto replacement = std::ranges::find_if(bucket.replacements, [](const auto& value) {
      return value.admission == dht::routing_admission::verified_server;
   });
   if (replacement == bucket.replacements.end()) {
      return;
   }
   bucket.active.push_back(std::move(*replacement));
   bucket.replacements.erase(replacement);
}

} // namespace

dht::routing_table::impl::impl(peer_id local_peer, dht::options options_value)
    : local(std::move(local_peer)), options(std::move(options_value)) {}

std::size_t dht::routing_table::impl::bucket_for(const peer_id& peer) const {
   return common_prefix_bits(distance_between(local.to_bytes(), peer.to_bytes()));
}

void dht::routing_table::impl::upsert(dht::peer value, dht::routing_admission admission) {
   if (!valid_peer_id(value.id) || value.id == local) {
      return;
   }

   auto lock = std::scoped_lock{mutex};
   auto& bucket = buckets[bucket_for(value.id)];
   if (auto current = find_entry(bucket.active, value.id); current != bucket.active.end()) {
      if (admission == dht::routing_admission::candidate) {
         return;
      }
      current->value = std::move(value);
      current->admission = dht::routing_admission::verified_server;
      current->failures = 0;
      current->sequence = next_sequence++;
      return;
   }

   auto entry = impl::entry{
       .value = std::move(value),
       .admission = admission,
       .sequence = next_sequence++,
   };
   if (admission == dht::routing_admission::verified_server && bucket.active.size() < options.replication) {
      bucket.replacements.erase(std::remove_if(bucket.replacements.begin(), bucket.replacements.end(),
                                               [&](const auto& current) { return current.value.id == entry.value.id; }),
                                bucket.replacements.end());
      bucket.active.push_back(std::move(entry));
      return;
   }
   insert_replacement(bucket, std::move(entry), options.replacement_cache_size);
}

void dht::routing_table::impl::mark_failure(const peer_id& peer) {
   auto lock = std::scoped_lock{mutex};
   auto& bucket = buckets[bucket_for(peer)];
   if (const auto current = find_entry(bucket.active, peer); current != bucket.active.end()) {
      if (++current->failures < options.failure_threshold) {
         return;
      }
      bucket.active.erase(current);
      promote_replacement(bucket);
      return;
   }
   const auto replacement = find_entry(bucket.replacements, peer);
   if (replacement != bucket.replacements.end() && ++replacement->failures >= options.failure_threshold) {
      bucket.replacements.erase(replacement);
   }
}

std::vector<dht::peer> dht::routing_table::impl::closest(std::span<const std::uint8_t> target,
                                                         std::size_t limit) const {
   auto entries = std::vector<std::pair<dht::distance, dht::peer>>{};
   {
      auto lock = std::scoped_lock{mutex};
      for (const auto& bucket : buckets) {
         for (const auto& value : bucket.active) {
            entries.emplace_back(distance_between(value.value.id.to_bytes(), target), value.value);
         }
      }
   }
   std::ranges::sort(entries, [](const auto& left, const auto& right) {
      return left.first != right.first ? left.first < right.first : left.second.id < right.second.id;
   });
   const auto count = std::min({limit, options.replication, entries.size()});
   auto out = std::vector<dht::peer>{};
   out.reserve(count);
   for (auto index = std::size_t{}; index < count; ++index) {
      out.push_back(std::move(entries[index].second));
   }
   return out;
}

std::vector<dht::peer> dht::routing_table::impl::query_seeds(std::span<const std::uint8_t> target,
                                                             std::size_t limit) const {
   auto active = std::vector<std::pair<dht::distance, dht::peer>>{};
   auto fallback = std::vector<std::pair<dht::distance, dht::peer>>{};
   {
      auto lock = std::scoped_lock{mutex};
      for (const auto& bucket : buckets) {
         for (const auto& value : bucket.active) {
            active.emplace_back(distance_between(value.value.id.to_bytes(), target), value.value);
         }
         for (const auto& value : bucket.replacements) {
            fallback.emplace_back(distance_between(value.value.id.to_bytes(), target), value.value);
         }
      }
   }
   const auto by_distance = [](const auto& left, const auto& right) {
      return left.first != right.first ? left.first < right.first : left.second.id < right.second.id;
   };
   std::ranges::sort(active, by_distance);
   std::ranges::sort(fallback, by_distance);
   const auto count = std::min({limit, options.replication, active.size() + fallback.size()});
   auto out = std::vector<dht::peer>{};
   out.reserve(count);
   for (auto& entry : active) {
      if (out.size() >= count) {
         break;
      }
      out.push_back(std::move(entry.second));
   }
   for (auto& entry : fallback) {
      if (out.size() >= count) {
         break;
      }
      out.push_back(std::move(entry.second));
   }
   return out;
}

std::vector<dht::peer> dht::routing_table::impl::snapshot() const {
   auto out = std::vector<dht::peer>{};
   auto lock = std::scoped_lock{mutex};
   for (const auto& bucket : buckets) {
      for (const auto& value : bucket.active) {
         out.push_back(value.value);
      }
   }
   std::ranges::sort(out, {}, &dht::peer::id);
   return out;
}

dht::routing_status dht::routing_table::impl::status() const {
   auto out = dht::routing_status{};
   auto lock = std::scoped_lock{mutex};
   for (const auto& bucket : buckets) {
      if (!bucket.active.empty() || !bucket.replacements.empty()) {
         ++out.nonempty_buckets;
      }
      out.active += bucket.active.size();
      out.replacements += bucket.replacements.size();
      out.candidates += static_cast<std::size_t>(std::ranges::count_if(
          bucket.replacements, [](const auto& value) { return value.admission == dht::routing_admission::candidate; }));
   }
   return out;
}

dht::routing_table::routing_table(peer_id local_peer, dht::options options_value)
    : impl_(std::make_unique<impl>(std::move(local_peer), std::move(options_value))) {
   if (impl_->options.replication == 0 || impl_->options.replacement_cache_size == 0 ||
       impl_->options.failure_threshold == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "DHT routing limits must be positive");
   }
}

dht::routing_table::~routing_table() = default;
dht::routing_table::routing_table(routing_table&&) noexcept = default;
dht::routing_table& dht::routing_table::operator=(routing_table&&) noexcept = default;

void dht::routing_table::upsert(peer value, routing_admission admission) {
   impl_->upsert(std::move(value), admission);
}

void dht::routing_table::mark_failure(const peer_id& peer) {
   impl_->mark_failure(peer);
}

std::vector<dht::peer> dht::routing_table::closest(std::span<const std::uint8_t> target, std::size_t limit) const {
   return impl_->closest(target, limit);
}

std::vector<dht::peer> dht::routing_table::query_seeds(std::span<const std::uint8_t> target, std::size_t limit) const {
   return impl_->query_seeds(target, limit);
}

std::vector<dht::peer> dht::routing_table::snapshot() const {
   return impl_->snapshot();
}

dht::routing_status dht::routing_table::status() const {
   return impl_->status();
}

} // namespace forge::net::p2p
