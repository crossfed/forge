module;

#include <boost/describe.hpp>

#include <cstdint>

export module forge.chain.quorum.types;

export namespace forge::chain::quorum {

enum class status : std::uint8_t {
   insufficient = 0,
   reached = 1,
};

struct result {
   std::uint64_t threshold = 0;
   std::uint64_t total_weight = 0;
   std::uint64_t signed_weight = 0;
   status state = status::insufficient;

   [[nodiscard]] bool reached() const noexcept {
      return state == status::reached;
   }

   bool operator==(const result&) const = default;
};

BOOST_DESCRIBE_ENUM(status, insufficient, reached)
BOOST_DESCRIBE_STRUCT(result, (), (threshold, total_weight, signed_weight, state))

} // namespace forge::chain::quorum
