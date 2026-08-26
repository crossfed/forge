module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>
#include <optional>

export module forge.chain.protocol.resource_meter;

export import forge.chain.protocol.time;

export namespace forge::chain::protocol {

struct resource_meter {
   std::uint64_t used = 0;
   std::optional<std::uint64_t> max;
   std::optional<std::uint64_t> available;
   std::uint32_t window = 0;
   std::uint32_t last_ordinal = 0;
   std::optional<time_point> fully_recovered_at;

   bool operator==(const resource_meter&) const = default;
};

[[nodiscard]] constexpr bool valid(const resource_meter& value) noexcept {
   return value.max.has_value() == value.available.has_value();
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(resource_meter, (), (used, max, available, window, last_ordinal, fully_recovered_at))
} // namespace forge::chain::protocol
#endif
