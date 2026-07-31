module;

#include <compare>
#include <cstdint>

export module forge.chain.protocol.typed_id;

export namespace forge::chain::protocol {

template <std::uint8_t Space, std::uint16_t Type>
struct typed_id {
   static constexpr std::uint8_t space = Space;
   static constexpr std::uint16_t type = Type;

   std::uint64_t instance = 0;

   constexpr bool operator==(const typed_id&) const = default;
   constexpr auto operator<=>(const typed_id&) const = default;
};

} // namespace forge::chain::protocol
