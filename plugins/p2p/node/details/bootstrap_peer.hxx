#pragma once

namespace forge::plugins::p2p::node {

struct bootstrap_peer {
   forge::net::p2p::endpoint endpoint;
   std::optional<forge::net::p2p::peer_id> peer;
   std::chrono::steady_clock::time_point next_attempt{};
   std::size_t failures = 0;
};

} // namespace forge::plugins::p2p::node
