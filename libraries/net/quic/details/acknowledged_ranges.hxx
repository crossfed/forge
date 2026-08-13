#pragma once

#include <cstdint>
#include <map>

namespace forge::net::quic::detail {

class acknowledged_ranges {
 public:
   void add(std::uint64_t offset, std::uint64_t length);
   [[nodiscard]] bool covers(std::uint64_t offset, std::uint64_t length) const noexcept;
   void discard_before(std::uint64_t offset);
   void clear() noexcept;

 private:
   std::map<std::uint64_t, std::uint64_t> ranges_;
};

} // namespace forge::net::quic::detail
