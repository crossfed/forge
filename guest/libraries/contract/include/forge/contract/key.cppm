module;

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module forge.contract.key;

export import forge.contract.datastream;
export import forge.contract.varint;

import forge.chain.protocol.values;
import forge.contract.intrinsics;

export namespace forge::contract {

using key_type = std::string;

namespace detail {

template <typename Stream> void key_byte(Stream& stream, std::uint8_t value) {
   const auto byte = static_cast<char>(value);
   stream.write(&byte, 1U);
}

template <typename T>
concept reflected_key = requires(const T& value) {
   value.forge_contract_for_each_field([](const auto&) {});
};

template <typename T>
concept key_range = std::ranges::input_range<T> && !std::same_as<std::remove_cvref_t<T>, std::string> &&
                    !std::same_as<std::remove_cvref_t<T>, std::string_view>;

template <typename UInt, typename Float> [[nodiscard]] UInt float_key(Float value) {
   auto bits = std::bit_cast<UInt>(value);
   const auto sign = UInt{1} << (std::numeric_limits<UInt>::digits - 1U);
   if (bits == sign) {
      bits = 0U;
   }
   return bits ^ ((bits & sign) != 0U ? std::numeric_limits<UInt>::max() : sign);
}

} // namespace detail

template <typename T, typename Stream> void to_key(const T& value, Stream& stream);

template <typename Stream> void to_key(std::string_view value, Stream& stream) {
   for (const auto character : value) {
      detail::key_byte(stream, static_cast<std::uint8_t>(character));
      if (character == '\0') {
         detail::key_byte(stream, 1U);
      }
   }
   detail::key_byte(stream, 0U);
   detail::key_byte(stream, 0U);
}

template <typename Stream> void to_key(const std::string& value, Stream& stream) {
   to_key(std::string_view{value}, stream);
}

template <std::size_t Size, typename Stream> void to_key(const char (&value)[Size], Stream& stream) {
   to_key(std::string_view{value, Size - 1U}, stream);
}

template <typename Stream> void to_key(bool value, Stream& stream) {
   detail::key_byte(stream, value ? 1U : 0U);
}

template <std::integral T, typename Stream>
   requires(!std::same_as<T, bool>)
void to_key(T value, Stream& stream) {
   using unsigned_type = std::make_unsigned_t<T>;
   auto adjusted = static_cast<unsigned_type>(value);
   if constexpr (std::is_signed_v<T>) {
      adjusted ^= unsigned_type{1} << (std::numeric_limits<unsigned_type>::digits - 1U);
   }
   for (auto index = sizeof(T); index > 0U; --index) {
      detail::key_byte(stream, static_cast<std::uint8_t>(adjusted >> ((index - 1U) * 8U)));
   }
}

template <std::floating_point T, typename Stream> void to_key(T value, Stream& stream) {
   if constexpr (sizeof(T) == sizeof(std::uint32_t)) {
      to_key(detail::float_key<std::uint32_t>(value), stream);
   } else {
      static_assert(sizeof(T) == sizeof(std::uint64_t), "unsupported floating-point key width");
      to_key(detail::float_key<std::uint64_t>(value), stream);
   }
}

template <typename Enum, typename Stream>
   requires std::is_enum_v<Enum>
void to_key(Enum value, Stream& stream) {
   to_key(static_cast<std::underlying_type_t<Enum>>(value), stream);
}

template <typename Stream> void to_key(chain::protocol::name value, Stream& stream) {
   to_key(value.value, stream);
}

template <typename First, typename Second, typename Stream>
void to_key(const std::pair<First, Second>& value, Stream& stream) {
   to_key(value.first, stream);
   to_key(value.second, stream);
}

template <typename... Values, typename Stream> void to_key(const std::tuple<Values...>& value, Stream& stream) {
   std::apply([&](const auto&... item) { (to_key(item, stream), ...); }, value);
}

template <typename T, std::size_t Size, typename Stream> void to_key(const std::array<T, Size>& value, Stream& stream) {
   for (const auto& item : value) {
      to_key(item, stream);
   }
}

template <typename T, typename Stream> void to_key(const std::optional<T>& value, Stream& stream) {
   detail::key_byte(stream, value ? 1U : 0U);
   if (value) {
      to_key(*value, stream);
   }
}

template <typename... Values, typename Stream> void to_key(const std::variant<Values...>& value, Stream& stream) {
   to_key(static_cast<std::uint32_t>(value.index()), stream);
   std::visit([&](const auto& item) { to_key(item, stream); }, value);
}

template <detail::key_range Range, typename Stream> void to_key(const Range& value, Stream& stream) {
   for (const auto& item : value) {
      detail::key_byte(stream, 1U);
      to_key(item, stream);
   }
   detail::key_byte(stream, 0U);
}

template <detail::reflected_key T, typename Stream> void to_key(const T& value, Stream& stream) {
   value.forge_contract_for_each_field([&](const auto& member) { to_key(member, stream); });
}

template <typename T> void convert_to_key(const T& value, key_type& result) {
   auto stream = ::forge::datastream<std::vector<char>>{};
   to_key(value, stream);
   const auto& bytes = stream.storage();
   result.append(bytes.data(), bytes.size());
}

template <typename T> [[nodiscard]] key_type convert_to_key(const T& value) {
   auto result = key_type{};
   convert_to_key(value, result);
   return result;
}

} // namespace forge::contract
