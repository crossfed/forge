module;

#include <compare>
#include <cstdint>
#include <type_traits>

export module forge.db.ids.typed_id;

import forge.raw.codec;

export namespace forge::db::ids {

template <std::uint8_t Space, std::uint16_t Type>
struct typed_id
{
   static constexpr std::uint8_t space = Space;
   static constexpr std::uint16_t type = Type;

   std::uint64_t instance = 0;

   constexpr typed_id() noexcept = default;
   constexpr explicit typed_id(std::uint64_t value) noexcept : instance{value} {}

   bool operator==(const typed_id&) const = default;
   auto operator<=>(const typed_id&) const = default;
};

template <typename T>
struct typed_id_traits {
   static constexpr bool is_typed_id = false;
};

template <std::uint8_t Space, std::uint16_t Type>
struct typed_id_traits<typed_id<Space, Type>> {
   static constexpr bool is_typed_id = true;
   static constexpr std::uint8_t space = Space;
   static constexpr std::uint16_t type = Type;
};

template <typename T>
concept typed_id_like = typed_id_traits<std::remove_cvref_t<T>>::is_typed_id;

template <typename Id>
struct type_for_id;

template <typename Id>
using type_for_id_t = typename type_for_id<std::remove_cvref_t<Id>>::type;

template <typename Stream, std::uint8_t Space, std::uint16_t Type>
Stream& operator<<(Stream& stream, const typed_id<Space, Type>& value) {
   forge::raw::pack(stream, value.instance);
   return stream;
}

template <typename Stream, std::uint8_t Space, std::uint16_t Type>
Stream& operator>>(Stream& stream, typed_id<Space, Type>& value) {
   auto instance = std::uint64_t{};
   forge::raw::unpack(stream, instance);
   value = typed_id<Space, Type>{instance};
   return stream;
}

} // namespace forge::db::ids
