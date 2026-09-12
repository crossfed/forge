module;

#include <boost/describe.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>

export module forge.net.p2p.dialing;

export namespace forge::net::p2p {

// Preview policy and diagnostics for direct TCP/QUIC dial planning.
struct dialing {
   struct ranker_policy {
      std::chrono::milliseconds public_delay{250};
      std::chrono::milliseconds private_delay{30};
   };

   enum class black_hole_state : std::uint8_t {
      probing,
      allowed,
      blocked,
   };

   // The caller classifies dial completion; neutral outcomes do not affect detection.
   enum class outcome : std::uint8_t {
      neutral,
      success,
      failure,
   };

   struct black_hole_policy {
      std::size_t window_size = 100;
      std::size_t min_successes = 5;
      bool udp_enabled = true;
      bool ipv6_enabled = true;
   };

   struct policy {
      ranker_policy ranker{};
      black_hole_policy black_holes{};
      std::size_t max_concurrent_attempts = 4;
   };

   struct black_hole_counter_status {
      bool enabled = false;
      black_hole_state state = black_hole_state::probing;
      std::size_t peer_requests = 0;
      std::size_t outcomes = 0;
      std::size_t successes = 0;
      std::size_t next_probe_after = 0;
   };

   struct black_hole_status {
      black_hole_counter_status udp;
      black_hole_counter_status ipv6;
   };
};

BOOST_DESCRIBE_ENUM(dialing::black_hole_state, probing, allowed, blocked)
BOOST_DESCRIBE_STRUCT(dialing::black_hole_counter_status, (),
                      (enabled, state, peer_requests, outcomes, successes, next_probe_after))
BOOST_DESCRIBE_STRUCT(dialing::black_hole_status, (), (udp, ipv6))

} // namespace forge::net::p2p
