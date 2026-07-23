#include <array>
#include <cstdint>

import forge.chain.quorum.evaluate;

int main() {
   const auto value = forge::chain::quorum::evaluate(
       3U, std::array<std::uint64_t, 2>{1U, 2U}, std::array<std::uint32_t, 2>{0U, 1U});
   return value.reached() ? 0 : 1;
}
