module;

#include <chrono>
#include <limits>

module forge.net.p2p.node;

#include "details/dht_time.hxx"

namespace forge::net::p2p::detail {

std::chrono::system_clock::time_point dht_expiry_after(std::chrono::system_clock::time_point now,
                                                       std::chrono::seconds ttl) noexcept {
   const auto converted = std::chrono::duration_cast<std::chrono::system_clock::duration>(ttl);
   const auto increment = converted.count();
   if (increment <= 0) {
      return now;
   }
   const auto base = now.time_since_epoch().count();
   const auto maximum = (std::numeric_limits<std::chrono::system_clock::duration::rep>::max)();
   if (base > maximum - increment) {
      return (std::chrono::system_clock::time_point::max)();
   }
   return std::chrono::system_clock::time_point{std::chrono::system_clock::duration{base + increment}};
}

} // namespace forge::net::p2p::detail
