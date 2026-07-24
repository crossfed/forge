module;

#include <array>
#include <cstdint>
#include <vector>

module forge.chain.savanna.vote;

namespace forge::chain::savanna {
namespace {

constexpr auto weak_postfix = std::array<std::uint8_t, 4>{'W', 'E', 'A', 'K'};

} // namespace

std::vector<std::uint8_t> message_for_vote(digest finality_digest, vote_kind kind) {
   const auto bytes = finality_digest.to_uint8_span();
   auto result = std::vector<std::uint8_t>{bytes.begin(), bytes.end()};
   if (kind == vote_kind::weak) {
      result.insert(result.end(), weak_postfix.begin(), weak_postfix.end());
   }
   return result;
}

} // namespace forge::chain::savanna
