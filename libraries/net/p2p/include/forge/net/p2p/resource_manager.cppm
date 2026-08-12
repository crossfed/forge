module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <boost/describe.hpp>

export module forge.net.p2p.resource_manager;

import forge.net.p2p.identity;
import forge.net.p2p.protocol;

export namespace forge::net::p2p {

class resource_manager {
 public:
   struct limits {
      std::size_t max_streams = 4096;
      std::size_t max_streams_per_peer = 256;
      std::size_t max_streams_per_protocol = 1024;
      std::size_t max_relay_reservations = 1024;
      std::size_t max_relay_streams = 128;
      std::uint64_t max_queued_bytes = 16 * 1024 * 1024;
      std::size_t max_dial_attempts_per_peer = 16;
      std::size_t max_malformed_messages_per_peer = 64;
      std::size_t max_pending_inbound_sessions = 1024;
      std::size_t max_pending_outbound_sessions = 1024;
      std::size_t max_inbound_sessions = 1024;
      std::size_t max_outbound_sessions = 1024;
      std::size_t max_sessions_per_peer = 4;
      std::size_t max_dial_attempts = 1024;
   };

   struct scope {
      peer_id peer;
      protocol_id protocol;
   };

   enum class session_direction { inbound, outbound };

   struct session_scope {
      peer_id peer;
      session_direction direction = session_direction::outbound;
   };

   struct snapshot {
      std::size_t active_streams = 0;
      std::size_t active_relay_streams = 0;
      std::size_t active_relay_reservations = 0;
      std::size_t pending_inbound_sessions = 0;
      std::size_t pending_outbound_sessions = 0;
      std::size_t active_inbound_sessions = 0;
      std::size_t active_outbound_sessions = 0;
      std::size_t active_peer_scopes = 0;
      std::size_t active_protocol_scopes = 0;
      std::size_t active_session_peer_scopes = 0;
      std::size_t dial_attempt_scopes = 0;
      std::size_t malformed_scopes = 0;
      std::uint64_t denied = 0;
      std::size_t active_dials = 0;
      std::uint64_t queued_bytes = 0;
      std::uint64_t denied_sessions = 0;
      std::uint64_t denied_dials = 0;
      std::uint64_t denied_streams = 0;
      std::uint64_t denied_queued_bytes = 0;
      std::uint64_t denied_relays = 0;
      std::uint64_t denied_malformed = 0;
   };

   class session_reservation;
   class dial_reservation;
   class stream_reservation;
   class queued_bytes_reservation;
   class relay_reservation;

   resource_manager();
   explicit resource_manager(limits value);
   ~resource_manager();

   [[nodiscard]] const limits& configured_limits() const noexcept;
   [[nodiscard]] snapshot current() const noexcept;
   [[nodiscard]] std::optional<session_reservation> reserve_session(session_direction direction) noexcept;
   [[nodiscard]] std::optional<dial_reservation> reserve_dial() noexcept;
   [[nodiscard]] std::optional<dial_reservation> reserve_dial(const peer_id& peer) noexcept;
   [[nodiscard]] std::optional<stream_reservation> reserve_stream() noexcept;
   [[nodiscard]] std::optional<stream_reservation> reserve_stream(const scope& value) noexcept;
   [[nodiscard]] std::optional<stream_reservation> reserve_relay_stream() noexcept;
   [[nodiscard]] std::optional<relay_reservation> reserve_relay(const scope& value) noexcept;
   [[nodiscard]] std::optional<queued_bytes_reservation> reserve_queued_bytes(std::uint64_t bytes) noexcept;
   [[nodiscard]] bool record_malformed(const scope& value) noexcept;

 private:
   struct state;
   std::shared_ptr<state> state_;
};

class resource_manager::session_reservation {
 public:
   session_reservation() noexcept;
   ~session_reservation();
   session_reservation(session_reservation&&) noexcept;
   session_reservation& operator=(session_reservation&&) noexcept;
   session_reservation(const session_reservation&) = delete;
   session_reservation& operator=(const session_reservation&) = delete;

   [[nodiscard]] bool active() const noexcept;
   [[nodiscard]] bool established() const noexcept;
   [[nodiscard]] bool establish(session_scope value) noexcept;
   void release() noexcept;

 private:
   friend class resource_manager;
   session_reservation(std::shared_ptr<state> owner, session_direction direction) noexcept;

   std::shared_ptr<state> owner_;
   session_direction direction_ = session_direction::outbound;
   std::optional<session_scope> scope_;
};

class resource_manager::dial_reservation {
 public:
   dial_reservation() noexcept;
   ~dial_reservation();
   dial_reservation(dial_reservation&&) noexcept;
   dial_reservation& operator=(dial_reservation&&) noexcept;
   dial_reservation(const dial_reservation&) = delete;
   dial_reservation& operator=(const dial_reservation&) = delete;

   [[nodiscard]] bool active() const noexcept;
   [[nodiscard]] bool bound() const noexcept;
   [[nodiscard]] bool bind(peer_id peer) noexcept;
   void release() noexcept;

 private:
   friend class resource_manager;
   explicit dial_reservation(std::shared_ptr<state> owner) noexcept;

   std::shared_ptr<state> owner_;
   std::optional<peer_id> peer_;
};

class resource_manager::stream_reservation {
 public:
   stream_reservation() noexcept;
   ~stream_reservation();
   stream_reservation(stream_reservation&&) noexcept;
   stream_reservation& operator=(stream_reservation&&) noexcept;
   stream_reservation(const stream_reservation&) = delete;
   stream_reservation& operator=(const stream_reservation&) = delete;

   [[nodiscard]] bool active() const noexcept;
   [[nodiscard]] bool bound() const noexcept;
   [[nodiscard]] bool bind(scope value) noexcept;
   void release() noexcept;

 private:
   friend class resource_manager;
   stream_reservation(std::shared_ptr<state> owner, bool relay) noexcept;

   std::shared_ptr<state> owner_;
   std::optional<scope> scope_;
   bool relay_ = false;
};

class resource_manager::queued_bytes_reservation {
 public:
   queued_bytes_reservation() noexcept;
   ~queued_bytes_reservation();
   queued_bytes_reservation(queued_bytes_reservation&&) noexcept;
   queued_bytes_reservation& operator=(queued_bytes_reservation&&) noexcept;
   queued_bytes_reservation(const queued_bytes_reservation&) = delete;
   queued_bytes_reservation& operator=(const queued_bytes_reservation&) = delete;

   [[nodiscard]] bool active() const noexcept;
   [[nodiscard]] std::uint64_t bytes() const noexcept;
   void release() noexcept;

 private:
   friend class resource_manager;
   queued_bytes_reservation(std::shared_ptr<state> owner, std::uint64_t bytes) noexcept;

   std::shared_ptr<state> owner_;
   std::uint64_t bytes_ = 0;
};

class resource_manager::relay_reservation {
 public:
   relay_reservation() noexcept;
   ~relay_reservation();
   relay_reservation(relay_reservation&&) noexcept;
   relay_reservation& operator=(relay_reservation&&) noexcept;
   relay_reservation(const relay_reservation&) = delete;
   relay_reservation& operator=(const relay_reservation&) = delete;

   [[nodiscard]] bool active() const noexcept;
   void release() noexcept;

 private:
   friend class resource_manager;
   relay_reservation(std::shared_ptr<state> owner, peer_id peer) noexcept;

   std::shared_ptr<state> owner_;
   peer_id peer_;
};

} // namespace forge::net::p2p

BOOST_DESCRIBE_STRUCT(forge::net::p2p::resource_manager::limits, (),
                      (max_streams, max_streams_per_peer, max_streams_per_protocol, max_relay_reservations,
                       max_relay_streams, max_queued_bytes, max_dial_attempts_per_peer,
                       max_malformed_messages_per_peer, max_pending_inbound_sessions, max_pending_outbound_sessions,
                       max_inbound_sessions, max_outbound_sessions, max_sessions_per_peer, max_dial_attempts))
