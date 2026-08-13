#pragma once

namespace forge::net::p2p {

struct resource_manager::state {
   explicit state(limits value) noexcept;

   [[nodiscard]] const limits& configured_limits() const noexcept;
   [[nodiscard]] snapshot current() const noexcept;
   [[nodiscard]] bool acquire_pending(session_direction direction) noexcept;
   [[nodiscard]] bool establish_session(session_scope value) noexcept;
   void release_pending(session_direction direction) noexcept;
   void release_session(const session_scope& value) noexcept;
   [[nodiscard]] bool acquire_dial() noexcept;
   [[nodiscard]] bool bind_dial(const peer_id& peer) noexcept;
   void release_dial(const std::optional<peer_id>& peer) noexcept;
   [[nodiscard]] bool acquire_stream(bool relay) noexcept;
   [[nodiscard]] bool bind_stream(const scope& value) noexcept;
   void release_stream(const std::optional<scope>& value, bool relay) noexcept;
   [[nodiscard]] bool acquire_relay(const peer_id& peer) noexcept;
   void release_relay(const peer_id& peer) noexcept;
   [[nodiscard]] bool acquire_queued_bytes(std::uint64_t bytes) noexcept;
   void release_queued_bytes(std::uint64_t bytes) noexcept;
   [[nodiscard]] bool record_malformed(const peer_id& peer) noexcept;

 private:
   [[nodiscard]] bool deny_locked(std::uint64_t& reason) noexcept;

   mutable std::mutex mutex_;
   limits limits_;
   snapshot snapshot_;
   std::map<peer_id, std::size_t> streams_by_peer_;
   std::map<std::string, std::size_t> streams_by_protocol_;
   std::map<peer_id, std::size_t> relay_reservations_by_peer_;
   std::map<peer_id, std::size_t> sessions_by_peer_;
   std::map<peer_id, std::size_t> dial_attempts_by_peer_;
   std::map<peer_id, std::size_t> malformed_by_peer_;
};

} // namespace forge::net::p2p
