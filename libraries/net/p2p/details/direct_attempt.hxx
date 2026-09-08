#pragma once

#include <chrono>
#include <memory>

#include "direct_transport.hxx"
#include "session_teardown.hxx"

namespace forge::net::p2p::detail {

struct direct_attempt_resources {
   // The transport retains this state until its native connection is gone.
   // Keep the teardown ticket first so reservations are released before the
   // node can observe this attempt as terminal.
   session_teardown::ticket teardown_ticket;
   resource_manager::session_reservation session;
   resource_manager::file_descriptor_reservation file_descriptor;
};

class direct_attempt final {
 public:
   // Owns an authenticated transport until exactly one winner commit publishes it.
   direct_attempt() = default;
   direct_attempt(const direct_attempt&) = delete;
   direct_attempt& operator=(const direct_attempt&) = delete;
   direct_attempt(direct_attempt&& other) noexcept;
   direct_attempt& operator=(direct_attempt&& other) noexcept;
   ~direct_attempt();

   void reset() noexcept;

   direct::connection connection;
   std::shared_ptr<direct_attempt_resources> resources;
   forge::net::p2p::endpoint target;
   std::chrono::steady_clock::time_point started_at{};
};

} // namespace forge::net::p2p::detail
