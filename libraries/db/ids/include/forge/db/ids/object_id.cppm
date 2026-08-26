module;

#include <compare>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>

export module forge.db.ids.object_id;

export import forge.db.ids.typed_id;

import forge.exceptions;
import forge.raw.raw;
import forge.variant.exceptions;
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
[[nodiscard]] constexpr object_id to_object_id(typed_id<Space, Type> value) noexcept {
   return object_id{.space = Space, .type = Type, .instance = value.instance};
}

template <std::uint8_t Space, std::uint16_t Type>
[[nodiscard]] std::optional<typed_id<Space, Type>> try_typed(object_id value) {
   if (!matches<Space, Type>(value)) {
      return std::nullopt;
   }
   return typed_id<Space, Type>{value.instance};
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
   try {
      auto instance = std::uint64_t{};
      forge::from_variant(input, instance);
      out = typed_id<Space, Type>{instance};
   } catch (const forge::exceptions::base&) {
      throw;
   } catch (const std::bad_cast&) {
      throw forge::variant_exceptions::decode_error{"typed ID variant must contain an integer"};
   } catch (const std::invalid_argument&) {
      throw forge::variant_exceptions::decode_error{"typed ID variant must contain an integer"};
   } catch (const std::out_of_range&) {
      throw forge::variant_exceptions::decode_error{"typed ID variant integer is out of range"};
   }
}

} // namespace forge::db::ids
