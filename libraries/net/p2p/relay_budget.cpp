module;

#include <cstdint>

module forge.net.p2p.node;

#include "details/relay_budget.hxx"

namespace forge::net::p2p::detail {

relay_budget::relay_budget(std::uint64_t limit) noexcept : limit_{limit} {}

bool relay_budget::consume(std::uint64_t bytes) noexcept {
   if (bytes > limit_ || used_ > limit_ - bytes) {
      return false;
   }
   used_ += bytes;
   return true;
}

bool relay_budget::exhausted() const noexcept {
   return used_ == limit_;
}

std::uint64_t relay_budget::used() const noexcept {
   return used_;
}

} // namespace forge::net::p2p::detail
