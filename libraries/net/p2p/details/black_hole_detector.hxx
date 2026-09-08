#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

namespace forge::net::p2p::detail {

struct black_hole_filter_result {
   std::vector<endpoint> allowed;
   std::vector<endpoint> blocked;
};

class black_hole_detector final {
 public:
   explicit black_hole_detector(dialing::black_hole_policy policy = {});

   static void validate_policy(const dialing::black_hole_policy& value);

   [[nodiscard]] black_hole_filter_result filter_peer_dial(std::vector<endpoint> values);
   void record_address_outcome(const endpoint& value, dialing::outcome outcome);
   [[nodiscard]] dialing::black_hole_status status() const;

 private:
   struct counter {
      bool enabled = false;
      std::size_t window_size = 0;
      std::size_t min_successes = 0;
      std::size_t peer_requests = 0;
      std::size_t successes = 0;
      dialing::black_hole_state state = dialing::black_hole_state::probing;
      std::vector<bool> outcomes;
   };

   static void record(counter& value, dialing::outcome outcome);
   static void reset(counter& value) noexcept;
   static void update_state(counter& value) noexcept;
   [[nodiscard]] static dialing::black_hole_state handle_request(counter& value) noexcept;
   [[nodiscard]] static dialing::black_hole_counter_status snapshot(const counter& value) noexcept;

   mutable std::mutex mutex_;
   counter udp_;
   counter ipv6_;
};

} // namespace forge::net::p2p::detail
