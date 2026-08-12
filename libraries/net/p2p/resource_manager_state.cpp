module;

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>

module forge.net.p2p.resource_manager;

#include "details/resource_manager_state.hxx"

namespace forge::net::p2p {

resource_manager::state::state(limits value) noexcept : limits_(value) {}

const resource_manager::limits& resource_manager::state::configured_limits() const noexcept {
   return limits_;
}

resource_manager::snapshot resource_manager::state::current() const noexcept {
   auto lock = std::scoped_lock{mutex_};
   auto out = snapshot_;
   out.active_peer_scopes = streams_by_peer_.size();
   out.active_protocol_scopes = streams_by_protocol_.size();
   out.active_session_peer_scopes = sessions_by_peer_.size();
   out.dial_attempt_scopes = dial_attempts_by_peer_.size();
   out.malformed_scopes = malformed_by_peer_.size();
   return out;
}

bool resource_manager::state::deny_locked(std::uint64_t& reason) noexcept {
   ++snapshot_.denied;
   ++reason;
   return false;
}

bool resource_manager::state::acquire_pending(session_direction direction) noexcept {
   auto lock = std::scoped_lock{mutex_};
   auto& current = direction == session_direction::inbound ? snapshot_.pending_inbound_sessions
                                                           : snapshot_.pending_outbound_sessions;
   const auto limit = direction == session_direction::inbound ? limits_.max_pending_inbound_sessions
                                                              : limits_.max_pending_outbound_sessions;
   if (current >= limit) {
      return deny_locked(snapshot_.denied_sessions);
   }
   ++current;
   return true;
}

bool resource_manager::state::establish_session(session_scope value) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (value.peer.value.empty()) {
      return deny_locked(snapshot_.denied_sessions);
   }
   const auto found = sessions_by_peer_.find(value.peer);
   const auto peer_sessions = found == sessions_by_peer_.end() ? 0 : found->second;
   const auto inbound = value.direction == session_direction::inbound;
   if (peer_sessions >= limits_.max_sessions_per_peer ||
       (inbound && snapshot_.active_inbound_sessions >= limits_.max_inbound_sessions) ||
       (!inbound && snapshot_.active_outbound_sessions >= limits_.max_outbound_sessions)) {
      return deny_locked(snapshot_.denied_sessions);
   }
   ++sessions_by_peer_[value.peer];
   if (inbound) {
      ++snapshot_.active_inbound_sessions;
   } else {
      ++snapshot_.active_outbound_sessions;
   }
   return true;
}

void resource_manager::state::release_pending(session_direction direction) noexcept {
   auto lock = std::scoped_lock{mutex_};
   auto& current = direction == session_direction::inbound ? snapshot_.pending_inbound_sessions
                                                           : snapshot_.pending_outbound_sessions;
   if (current > 0) {
      --current;
   }
}

void resource_manager::state::release_session(const session_scope& value) noexcept {
   auto lock = std::scoped_lock{mutex_};
   auto& current = value.direction == session_direction::inbound ? snapshot_.active_inbound_sessions
                                                                 : snapshot_.active_outbound_sessions;
   if (current > 0) {
      --current;
   }
   if (auto found = sessions_by_peer_.find(value.peer); found != sessions_by_peer_.end()) {
      if (found->second > 1) {
         --found->second;
      } else {
         sessions_by_peer_.erase(found);
      }
   }
}

bool resource_manager::state::acquire_dial() noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (snapshot_.active_dials >= limits_.max_dial_attempts) {
      return deny_locked(snapshot_.denied_dials);
   }
   ++snapshot_.active_dials;
   return true;
}

bool resource_manager::state::bind_dial(const peer_id& peer) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (peer.value.empty()) {
      return deny_locked(snapshot_.denied_dials);
   }
   auto& attempts = dial_attempts_by_peer_[peer];
   if (attempts >= limits_.max_dial_attempts_per_peer) {
      return deny_locked(snapshot_.denied_dials);
   }
   ++attempts;
   return true;
}

void resource_manager::state::release_dial(const std::optional<peer_id>& peer) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (snapshot_.active_dials > 0) {
      --snapshot_.active_dials;
   }
   if (!peer) {
      return;
   }
   if (auto found = dial_attempts_by_peer_.find(*peer); found != dial_attempts_by_peer_.end()) {
      if (found->second > 1) {
         --found->second;
      } else {
         dial_attempts_by_peer_.erase(found);
      }
   }
}

bool resource_manager::state::acquire_stream(bool relay) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (snapshot_.active_streams >= limits_.max_streams ||
       (relay && snapshot_.active_relay_streams >= limits_.max_relay_streams)) {
      return deny_locked(snapshot_.denied_streams);
   }
   ++snapshot_.active_streams;
   if (relay) {
      ++snapshot_.active_relay_streams;
   }
   return true;
}

bool resource_manager::state::bind_stream(const scope& value) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (value.peer.value.empty() || value.protocol.value.empty()) {
      return deny_locked(snapshot_.denied_streams);
   }
   const auto peer = streams_by_peer_.find(value.peer);
   const auto protocol = streams_by_protocol_.find(value.protocol.value);
   const auto peer_streams = peer == streams_by_peer_.end() ? 0 : peer->second;
   const auto protocol_streams = protocol == streams_by_protocol_.end() ? 0 : protocol->second;
   if (peer_streams >= limits_.max_streams_per_peer || protocol_streams >= limits_.max_streams_per_protocol) {
      return deny_locked(snapshot_.denied_streams);
   }
   ++streams_by_peer_[value.peer];
   ++streams_by_protocol_[value.protocol.value];
   return true;
}

void resource_manager::state::release_stream(const std::optional<scope>& value, bool relay) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (snapshot_.active_streams > 0) {
      --snapshot_.active_streams;
   }
   if (relay && snapshot_.active_relay_streams > 0) {
      --snapshot_.active_relay_streams;
   }
   if (!value) {
      return;
   }
   if (auto found = streams_by_peer_.find(value->peer); found != streams_by_peer_.end()) {
      if (found->second > 1) {
         --found->second;
      } else {
         streams_by_peer_.erase(found);
      }
   }
   if (auto found = streams_by_protocol_.find(value->protocol.value); found != streams_by_protocol_.end()) {
      if (found->second > 1) {
         --found->second;
      } else {
         streams_by_protocol_.erase(found);
      }
   }
}

bool resource_manager::state::acquire_relay(const peer_id& peer) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (peer.value.empty() || snapshot_.active_relay_reservations >= limits_.max_relay_reservations) {
      return deny_locked(snapshot_.denied_relays);
   }
   ++snapshot_.active_relay_reservations;
   ++relay_reservations_by_peer_[peer];
   return true;
}

void resource_manager::state::release_relay(const peer_id& peer) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (snapshot_.active_relay_reservations > 0) {
      --snapshot_.active_relay_reservations;
   }
   if (auto found = relay_reservations_by_peer_.find(peer); found != relay_reservations_by_peer_.end()) {
      if (found->second > 1) {
         --found->second;
      } else {
         relay_reservations_by_peer_.erase(found);
      }
   }
}

bool resource_manager::state::acquire_queued_bytes(std::uint64_t bytes) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (bytes > limits_.max_queued_bytes || snapshot_.queued_bytes > limits_.max_queued_bytes - bytes) {
      return deny_locked(snapshot_.denied_queued_bytes);
   }
   snapshot_.queued_bytes += bytes;
   return true;
}

void resource_manager::state::release_queued_bytes(std::uint64_t bytes) noexcept {
   auto lock = std::scoped_lock{mutex_};
   snapshot_.queued_bytes = bytes > snapshot_.queued_bytes ? 0 : snapshot_.queued_bytes - bytes;
}

bool resource_manager::state::record_malformed(const peer_id& peer) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (peer.value.empty()) {
      return deny_locked(snapshot_.denied_malformed);
   }
   auto& count = malformed_by_peer_[peer];
   if (count >= limits_.max_malformed_messages_per_peer) {
      return deny_locked(snapshot_.denied_malformed);
   }
   ++count;
   return true;
}

} // namespace forge::net::p2p
