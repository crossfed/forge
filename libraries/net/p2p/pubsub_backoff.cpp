module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include <map>
#include <string>

module forge.net.p2p.node;

import forge.net.p2p.identity;

#include "details/pubsub_backoff.hxx"

namespace forge::net::p2p::detail {

std::size_t pubsub_backoff::limit_for(std::size_t max_topics, std::size_t max_sessions) noexcept {
   if (max_topics == 0 || max_sessions == 0) {
      return 0;
   }
   if (max_topics > (std::numeric_limits<std::size_t>::max)() / max_sessions) {
      return (std::numeric_limits<std::size_t>::max)();
   }
   return max_topics * max_sessions;
}

std::chrono::seconds pubsub_backoff::remote_duration(std::chrono::seconds requested,
                                                     std::chrono::seconds fallback) noexcept {
   constexpr auto max_remote_backoff = std::chrono::hours{1};
   if (requested.count() <= 0) {
      return fallback;
   }
   return std::min(requested, std::chrono::duration_cast<std::chrono::seconds>(max_remote_backoff));
}

pubsub_backoff::clock::time_point pubsub_backoff::deadline(clock::time_point now,
                                                           std::chrono::seconds duration) noexcept {
   if (duration.count() <= 0) {
      return now;
   }
   const auto remaining = clock::time_point::max() - now;
   if (duration >= std::chrono::duration_cast<std::chrono::seconds>(remaining)) {
      return clock::time_point::max();
   }
   return now + std::chrono::duration_cast<clock::duration>(duration);
}

void pubsub_backoff::expire(clock::time_point now) noexcept {
   for (auto topic = entries_.begin(); topic != entries_.end();) {
      for (auto peer = topic->second.begin(); peer != topic->second.end();) {
         if (peer->second.local_until <= now && peer->second.remote_until <= now) {
            peer = topic->second.erase(peer);
            --size_;
         } else {
            ++peer;
         }
      }
      if (topic->second.empty()) {
         topic = entries_.erase(topic);
      } else {
         ++topic;
      }
   }
   if (local_saturated_until_ <= now) {
      local_saturated_until_ = {};
   }
   if (remote_saturated_until_ <= now) {
      remote_saturated_until_ = {};
   }
}

void pubsub_backoff::record_local(const std::string& topic, const peer_id& peer, std::chrono::seconds duration,
                                  clock::time_point now, std::size_t limit) noexcept {
   record(direction::local, topic, peer, duration, now, limit);
}

void pubsub_backoff::record_remote(const std::string& topic, const peer_id& peer, std::chrono::seconds duration,
                                   clock::time_point now, std::size_t limit) noexcept {
   record(direction::remote, topic, peer, duration, now, limit);
}

void pubsub_backoff::record(direction kind, const std::string& topic, const peer_id& peer,
                            std::chrono::seconds duration, clock::time_point now, std::size_t limit) noexcept {
   const auto until = deadline(now, duration);
   if (until <= now) {
      return;
   }

   const auto topic_found = entries_.find(topic);
   if (topic_found != entries_.end()) {
      const auto peer_found = topic_found->second.find(peer);
      if (peer_found != topic_found->second.end()) {
         auto& current = kind == direction::local ? peer_found->second.local_until : peer_found->second.remote_until;
         current = std::max(current, until);
         return;
      }
   }

   if (size_ >= limit) {
      auto& saturated_until =
          kind == direction::local ? local_saturated_until_ : remote_saturated_until_;
      saturated_until = std::max(saturated_until, until);
      return;
   }

   auto& value = entries_[topic][peer];
   if (kind == direction::local) {
      value.local_until = until;
   } else {
      value.remote_until = until;
   }
   ++size_;
}

pubsub_backoff::status pubsub_backoff::local_status(const std::string& topic, const peer_id& peer,
                                                    clock::time_point now) const noexcept {
   return get_status(direction::local, topic, peer, now);
}

pubsub_backoff::status pubsub_backoff::remote_status(const std::string& topic, const peer_id& peer,
                                                     clock::time_point now) const noexcept {
   return get_status(direction::remote, topic, peer, now);
}

pubsub_backoff::status pubsub_backoff::get_status(direction kind, const std::string& topic, const peer_id& peer,
                                                  clock::time_point now) const noexcept {
   if (const auto topic_found = entries_.find(topic); topic_found != entries_.end()) {
      if (const auto peer_found = topic_found->second.find(peer); peer_found != topic_found->second.end()) {
         const auto until = kind == direction::local ? peer_found->second.local_until : peer_found->second.remote_until;
         if (now < until) {
            return status::exact;
         }
      }
   }
   const auto saturated_until = kind == direction::local ? local_saturated_until_ : remote_saturated_until_;
   return now < saturated_until ? status::saturated : status::none;
}

bool pubsub_backoff::blocked(const std::string& topic, const peer_id& peer, clock::time_point now) const noexcept {
   return local_status(topic, peer, now) != status::none || remote_status(topic, peer, now) != status::none;
}

std::size_t pubsub_backoff::size() const noexcept {
   return size_;
}

} // namespace forge::net::p2p::detail
