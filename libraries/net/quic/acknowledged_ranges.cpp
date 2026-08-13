#include "details/acknowledged_ranges.hxx"

#include <algorithm>
#include <limits>

namespace forge::net::quic::detail {

void acknowledged_ranges::add(std::uint64_t offset, std::uint64_t length) {
   if (length == 0) {
      return;
   }
   const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
   auto end = length > maximum - offset ? maximum : offset + length;
   auto next = ranges_.lower_bound(offset);
   if (next != ranges_.begin()) {
      const auto previous = std::prev(next);
      if (previous->second >= offset) {
         offset = previous->first;
         end = std::max(end, previous->second);
         next = ranges_.erase(previous);
      }
   }
   while (next != ranges_.end() && next->first <= end) {
      end = std::max(end, next->second);
      next = ranges_.erase(next);
   }
   ranges_.emplace_hint(next, offset, end);
}

bool acknowledged_ranges::covers(std::uint64_t offset, std::uint64_t length) const noexcept {
   if (length == 0) {
      return true;
   }
   const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
   const auto end = length > maximum - offset ? maximum : offset + length;
   const auto next = ranges_.upper_bound(offset);
   if (next == ranges_.begin()) {
      return false;
   }
   const auto range = std::prev(next);
   return range->first <= offset && range->second >= end;
}

void acknowledged_ranges::discard_before(std::uint64_t offset) {
   auto next = ranges_.begin();
   while (next != ranges_.end() && next->second <= offset) {
      next = ranges_.erase(next);
   }
   if (next != ranges_.end() && next->first < offset) {
      const auto end = next->second;
      ranges_.erase(next);
      ranges_.emplace(offset, end);
   }
}

void acknowledged_ranges::clear() noexcept {
   ranges_.clear();
}

} // namespace forge::net::quic::detail
