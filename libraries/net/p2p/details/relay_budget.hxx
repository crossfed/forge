#pragma once

#include <cstdint>

namespace forge::net::p2p::detail {

class relay_budget {
 public:
   explicit relay_budget(std::uint64_t limit) noexcept;

   [[nodiscard]] bool consume(std::uint64_t bytes) noexcept;
   [[nodiscard]] bool exhausted() const noexcept;
   [[nodiscard]] std::uint64_t used() const noexcept;

 private:
   std::uint64_t limit_ = 0;
   std::uint64_t used_ = 0;
};

} // namespace forge::net::p2p::detail
