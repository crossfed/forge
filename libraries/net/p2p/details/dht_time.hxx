#pragma once

#include <chrono>

namespace forge::net::p2p::detail {

[[nodiscard]] std::chrono::system_clock::time_point dht_expiry_after(std::chrono::system_clock::time_point now,
                                                                     std::chrono::seconds ttl) noexcept;

} // namespace forge::net::p2p::detail
