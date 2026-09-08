#pragma once

#include <chrono>
#include <vector>

namespace forge::net::p2p::detail {

struct dial_plan_item {
   endpoint value;
   std::chrono::milliseconds delay{};
   bool tcp = false;
   // Applied only after a future scheduler observes TCP handshake progress.
   std::chrono::milliseconds tcp_handshake_progress_hold{};
};

class dial_ranker final {
 public:
   explicit dial_ranker(dialing::ranker_policy policy = {});

   static void validate_policy(const dialing::ranker_policy& value);

   [[nodiscard]] std::vector<dial_plan_item> rank(std::vector<endpoint> values) const;

 private:
   std::chrono::milliseconds public_delay_;
   std::chrono::milliseconds private_delay_;
};

} // namespace forge::net::p2p::detail
