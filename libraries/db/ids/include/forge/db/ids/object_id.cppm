module;

#include <compare>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

export module forge.db.ids.object_id;

import forge.raw.raw;
import forge.variant.value;

export namespace forge::db::ids {

struct object_id
{
   std::uint8_t space = 0;
   std::uint16_t type = 0;
   std::uint64_t instance = 0;

   bool operator==(const object_id&) const = default;
   auto operator<=>(const object_id&) const = default;
};

void to_variant(const object_id& value, forge::variant& out);
void from_variant(const forge::variant& input, object_id& out);

template <typename Stream> Stream& operator<<(Stream& stream, const object_id& value) {
   forge::raw::pack(stream, value.space);
   forge::raw::pack(stream, value.type);
   forge::raw::pack(stream, value.instance);
   return stream;
}

template <typename Stream> Stream& operator>>(Stream& stream, object_id& value) {
   forge::raw::unpack(stream, value.space);
   forge::raw::unpack(stream, value.type);
   forge::raw::unpack(stream, value.instance);
   return stream;
}

template <std::uint8_t Space, std::uint16_t Type>
[[nodiscard]] constexpr bool matches(object_id value) noexcept {
   return value.space == Space && value.type == Type;
}

template <std::uint8_t Space, std::uint16_t Type>
struct typed_id
{
   static constexpr std::uint8_t space = Space;
   static constexpr std::uint16_t type = Type;

   std::uint64_t instance = 0;

   constexpr typed_id() noexcept = default;
   constexpr explicit typed_id(std::uint64_t value) noexcept : instance{value} {}

   constexpr explicit typed_id(object_id input) {
      if (!matches<Space, Type>(input)) {
         throw std::invalid_argument("object_id space/type does not match typed_id");
      }
      instance = input.instance;
   }

   [[nodiscard]] constexpr object_id as_object_id() const noexcept {
      return object_id{.space = Space, .type = Type, .instance = instance};
   }

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

template <std::uint8_t Space, std::uint16_t Type>
[[nodiscard]] std::optional<typed_id<Space, Type>> try_typed(object_id value) {
   if (!matches<Space, Type>(value)) {
      return std::nullopt;
   }
   return typed_id<Space, Type>{value};
}

[[nodiscard]] std::string to_string(object_id value);

template <std::uint8_t Space, std::uint16_t Type> [[nodiscard]] std::string to_string(typed_id<Space, Type> value) {
   return std::to_string(static_cast<std::uint64_t>(Space)) + "/"
        + std::to_string(static_cast<std::uint64_t>(Type)) + "/" + std::to_string(value.instance);
}

template <std::uint8_t Space, std::uint16_t Type> void to_variant(const typed_id<Space, Type>& value, forge::variant& out) {
   out = forge::variant{static_cast<std::uint64_t>(value.instance)};
}

template <std::uint8_t Space, std::uint16_t Type> void from_variant(const forge::variant& input, typed_id<Space, Type>& out) {
   auto instance = std::uint64_t{};
   forge::from_variant(input, instance);
   out = typed_id<Space, Type>{instance};
}

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
