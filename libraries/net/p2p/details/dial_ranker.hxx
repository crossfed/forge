#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

namespace forge::net::p2p::detail {

struct resolved_dial_target {
   endpoint concrete;
   std::vector<std::size_t> root_indices;
};

struct dial_plan_item {
   endpoint value;
   std::vector<std::size_t> root_indices;
   std::chrono::milliseconds delay{};
   bool tcp = false;
   // Applied only after a future scheduler observes TCP handshake progress.
   std::chrono::milliseconds tcp_handshake_progress_hold{};
};

class dial_ranker final {
 public:
   explicit dial_ranker(dialing::ranker_policy policy = {});

   static void validate_policy(const dialing::ranker_policy& value);

   [[nodiscard]] std::vector<dial_plan_item> rank(std::vector<resolved_dial_target> values) const;

 private:
   std::chrono::milliseconds public_delay_;
   std::chrono::milliseconds private_delay_;
};

} // namespace forge::net::p2p::detail
