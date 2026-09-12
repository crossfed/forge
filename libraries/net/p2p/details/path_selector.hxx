#pragma once

#include <chrono>
#include <vector>

namespace forge::net::p2p::path_selector {

[[nodiscard]] bool supported_direct(const forge::multiformats::multiaddr& address);

[[nodiscard]] std::vector<peer_store::endpoint_record>
rank_direct(const peer_store::record& record, std::chrono::system_clock::time_point now);

} // namespace forge::net::p2p::path_selector
