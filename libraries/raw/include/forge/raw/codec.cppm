module;

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module forge.raw.codec;

export import forge.raw.stream;
export import forge.raw.varint_value;

export namespace forge::raw {

using bytes = std::vector<std::uint8_t>;

template <typename T> struct codec_traits {};

inline constexpr auto max_array_elements = std::uint32_t{1024U * 1024U};
inline constexpr auto max_byte_array_size = std::uint32_t{20U * 1024U * 1024U};

static_assert(CHAR_BIT == 8, "Forge raw serialization requires 8-bit bytes");

namespace detail {

[[noreturn]] void fail_codec(const char* message);

inline void require(bool condition, const char* message) {
   if (!condition) {
      fail_codec(message);
   }
}

template <typename Stream> void write_bytes(Stream& stream, std::span<const std::byte> value) {
   stream.write(reinterpret_cast<const char*>(value.data()), value.size());
}

template <typename Stream> void read_bytes(Stream& stream, std::span<std::byte> value) {
   stream.read(reinterpret_cast<char*>(value.data()), value.size());
}

template <typename Stream, typename T> void write_object(Stream& stream, const T& value) {
   write_bytes(stream, std::as_bytes(std::span{&value, std::size_t{1}}));
}

template <typename Stream, typename T> void read_object(Stream& stream, T& value) {
   read_bytes(stream, std::as_writable_bytes(std::span{&value, std::size_t{1}}));
}

template <typename Stream, typename T>
concept adl_packable = requires(Stream& stream, const T& value) { raw_pack(stream, value); };

template <typename Stream, typename T>
concept adl_unpackable = requires(Stream& stream, T& value) { raw_unpack(stream, value); };

template <typename T> struct built_in_codec : std::false_type {};

template <> struct built_in_codec<std::string> : std::true_type {};

template <typename T, typename Allocator> struct built_in_codec<std::vector<T, Allocator>> : std::true_type {};

template <typename T, typename Allocator> struct built_in_codec<std::deque<T, Allocator>> : std::true_type {};

template <typename T, typename Allocator> struct built_in_codec<std::list<T, Allocator>> : std::true_type {};

template <typename Key, typename Compare, typename Allocator>
struct built_in_codec<std::set<Key, Compare, Allocator>> : std::true_type {};

template <typename Key, typename Value, typename Compare, typename Allocator>
struct built_in_codec<std::map<Key, Value, Compare, Allocator>> : std::true_type {};

template <typename T> struct built_in_codec<std::optional<T>> : std::true_type {};

template <typename First, typename Second> struct built_in_codec<std::pair<First, Second>> : std::true_type {};

template <typename... T> struct built_in_codec<std::tuple<T...>> : std::true_type {};

template <typename T, std::size_t Size> struct built_in_codec<std::array<T, Size>> : std::true_type {};

template <typename... T> struct built_in_codec<std::variant<T...>> : std::true_type {};

template <> struct built_in_codec<unsigned_int> : std::true_type {};

template <> struct built_in_codec<signed_int> : std::true_type {};

template <> struct built_in_codec<bool> : std::true_type {};

template <typename T> inline constexpr auto built_in_codec_v = built_in_codec<std::remove_cv_t<T>>::value;

template <typename Stream, typename T>
concept stream_packable = !std::is_arithmetic_v<T> && !std::is_enum_v<T> && !built_in_codec_v<T> &&
                          requires(Stream& stream, const T& value) { stream << value; };

template <typename Stream, typename T>
concept stream_unpackable = !std::is_arithmetic_v<T> && !std::is_enum_v<T> && !built_in_codec_v<T> &&
                            requires(Stream& stream, T& value) { stream >> value; };

template <typename Stream, typename T>
concept traits_packable =
    requires(Stream& stream, const T& value) { codec_traits<std::remove_cv_t<T>>::pack(stream, value); };

template <typename Stream, typename T>
concept traits_unpackable =
    requires(Stream& stream, T& value) { codec_traits<std::remove_cv_t<T>>::unpack(stream, value); };

template <std::size_t Index = 0, typename... T> void select_variant(std::variant<T...>& value, std::size_t selected) {
   if constexpr (Index < sizeof...(T)) {
      if (Index == selected) {
         value.template emplace<Index>();
         return;
      }
      select_variant<Index + 1>(value, selected);
   } else {
      fail_codec("raw variant index is out of range");
   }
}

template <typename Stream> void pack_unsigned_int(Stream& stream, std::uint32_t value) {
   auto encoded = static_cast<std::uint64_t>(value);
   do {
      auto byte = static_cast<std::uint8_t>(encoded & 0x7fU);
      encoded >>= 7U;
      byte |= static_cast<std::uint8_t>((encoded > 0U) << 7U);
      write_object(stream, byte);
   } while (encoded != 0U);
}

template <typename Stream> std::uint32_t unpack_unsigned_int(Stream& stream) {
   auto encoded = std::uint32_t{0};
   auto byte = char{0};
   auto shift = std::uint8_t{0};
   do {
      require(shift < 35U, "raw unsigned varint is too long");
      stream.get(byte);
      const auto octet = static_cast<std::uint8_t>(byte);
      if (shift == 28U) {
         require((octet & 0xf0U) == 0U, "raw unsigned varint overflows uint32");
      }
      encoded |= static_cast<std::uint32_t>(octet & 0x7fU) << shift;
      shift += 7U;
   } while ((static_cast<std::uint8_t>(byte) & 0x80U) != 0U);
   return encoded;
}

} // namespace detail

template <typename Stream, typename T>
   requires(std::is_arithmetic_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
void pack(Stream& stream, const T& value) {
   detail::write_object(stream, value);
}

template <typename Stream, typename T>
   requires(std::is_arithmetic_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
void unpack(Stream& stream, T& value) {
   detail::read_object(stream, value);
}

template <typename Stream, typename T>
   requires(std::is_enum_v<T> && !detail::traits_packable<Stream, T>)
void pack(Stream& stream, const T& value) {
   const auto encoded = static_cast<std::underlying_type_t<T>>(value);
   detail::write_object(stream, encoded);
}

template <typename Stream, typename T>
   requires(std::is_enum_v<T> && !detail::traits_unpackable<Stream, T>)
void unpack(Stream& stream, T& value) {
   auto encoded = std::underlying_type_t<T>{};
   detail::read_object(stream, encoded);
   value = static_cast<T>(encoded);
}

template <typename Stream> void pack(Stream& stream, const std::byte& value) {
   detail::write_object(stream, value);
}

template <typename Stream> void unpack(Stream& stream, std::byte& value) {
   detail::read_object(stream, value);
}

template <typename Stream, typename T>
   requires detail::adl_packable<Stream, T>
void pack(Stream& stream, const T& value) {
   raw_pack(stream, value);
}

template <typename Stream, typename T>
   requires detail::adl_unpackable<Stream, T>
void unpack(Stream& stream, T& value) {
   raw_unpack(stream, value);
}

template <typename Stream, typename T>
   requires(detail::stream_packable<Stream, T> && !detail::adl_packable<Stream, T>)
void pack(Stream& stream, const T& value) {
   stream << value;
}

template <typename Stream, typename T>
   requires(detail::stream_unpackable<Stream, T> && !detail::adl_unpackable<Stream, T>)
void unpack(Stream& stream, T& value) {
   stream >> value;
}

template <typename Stream, typename T>
   requires(detail::traits_packable<Stream, T> && !detail::adl_packable<Stream, T> &&
            !detail::stream_packable<Stream, T>)
void pack(Stream& stream, const T& value) {
   codec_traits<std::remove_cv_t<T>>::pack(stream, value);
}

template <typename Stream, typename T>
   requires(detail::traits_unpackable<Stream, T> && !detail::adl_unpackable<Stream, T> &&
            !detail::stream_unpackable<Stream, T>)
void unpack(Stream& stream, T& value) {
   codec_traits<std::remove_cv_t<T>>::unpack(stream, value);
}

template <typename Stream> void pack(Stream& stream, const std::string& value);

template <typename Stream> void unpack(Stream& stream, std::string& value);

template <typename Stream, typename T> void pack(Stream& stream, const std::vector<T>& value);

template <typename Stream, typename T> void unpack(Stream& stream, std::vector<T>& value);

template <typename Stream, typename T, typename Allocator>
void pack(Stream& stream, const std::deque<T, Allocator>& value);

template <typename Stream, typename T, typename Allocator> void unpack(Stream& stream, std::deque<T, Allocator>& value);

template <typename Stream, typename T, typename Allocator>
void pack(Stream& stream, const std::list<T, Allocator>& value);

template <typename Stream, typename T, typename Allocator> void unpack(Stream& stream, std::list<T, Allocator>& value);

template <typename Stream, typename Key, typename Compare, typename Allocator>
void pack(Stream& stream, const std::set<Key, Compare, Allocator>& value);

template <typename Stream, typename Key, typename Compare, typename Allocator>
void unpack(Stream& stream, std::set<Key, Compare, Allocator>& value);

template <typename Stream, typename Key, typename Value, typename Compare, typename Allocator>
void pack(Stream& stream, const std::map<Key, Value, Compare, Allocator>& value);

template <typename Stream, typename Key, typename Value, typename Compare, typename Allocator>
void unpack(Stream& stream, std::map<Key, Value, Compare, Allocator>& value);

template <typename Stream, typename T> void pack(Stream& stream, const std::optional<T>& value);

template <typename Stream, typename T> void unpack(Stream& stream, std::optional<T>& value);

template <typename Stream, typename First, typename Second>
void pack(Stream& stream, const std::pair<First, Second>& value);

template <typename Stream, typename First, typename Second>
void unpack(Stream& stream, std::pair<First, Second>& value);

template <typename Stream, typename... T> void pack(Stream& stream, const std::tuple<T...>& value);

template <typename Stream, typename... T> void unpack(Stream& stream, std::tuple<T...>& value);

template <typename Stream, typename T, std::size_t Size> void pack(Stream& stream, const std::array<T, Size>& value);

template <typename Stream, typename T, std::size_t Size> void unpack(Stream& stream, std::array<T, Size>& value);

template <typename Stream, typename... T> void pack(Stream& stream, const std::variant<T...>& value);

template <typename Stream, typename... T> void unpack(Stream& stream, std::variant<T...>& value);

template <typename Stream> void pack(Stream& stream, const bool& value);

template <typename Stream> void unpack(Stream& stream, bool& value);

template <typename Stream> void pack(Stream& stream, const signed_int& value) {
   auto encoded = (static_cast<std::uint32_t>(value.value) << 1U) ^ static_cast<std::uint32_t>(value.value >> 31U);
   do {
      auto byte = static_cast<std::uint8_t>(encoded & 0x7fU);
      encoded >>= 7U;
      byte |= static_cast<std::uint8_t>((encoded > 0U) << 7U);
      detail::write_object(stream, byte);
   } while (encoded != 0U);
}

template <typename Stream> void pack(Stream& stream, const unsigned_int& value) {
   detail::pack_unsigned_int(stream, value.value);
}

template <typename Stream> void unpack(Stream& stream, signed_int& value) {
   auto encoded = std::uint32_t{0};
   auto byte = std::uint8_t{0};
   auto shift = std::uint8_t{0};
   do {
      detail::require(shift < 35U, "raw signed varint is too long");
      stream.get(byte);
      encoded |= static_cast<std::uint32_t>(byte & 0x7fU) << shift;
      shift += 7U;
   } while ((byte & 0x80U) != 0U);
   value.value = static_cast<std::int32_t>((encoded >> 1U) ^ (0U - (encoded & 1U)));
}

template <typename Stream> void unpack(Stream& stream, unsigned_int& value) {
   value.value = detail::unpack_unsigned_int(stream);
}

template <typename Stream> void pack(Stream& stream, const bool& value) {
   pack(stream, static_cast<std::uint8_t>(value));
}

template <typename Stream> void unpack(Stream& stream, bool& value) {
   auto encoded = std::uint8_t{0};
   unpack(stream, encoded);
   detail::require(encoded <= 1U, "raw bool is not canonical");
   value = encoded != 0U;
}

template <typename Stream> void pack(Stream& stream, const char* value) {
   const auto size = std::strlen(value);
   detail::require(size <= max_byte_array_size, "raw string exceeds the byte limit");
   detail::pack_unsigned_int(stream, static_cast<std::uint32_t>(size));
   if (size != 0U) {
      stream.write(value, size);
   }
}

template <typename Stream> void pack(Stream& stream, const std::string& value) {
   detail::require(value.size() <= max_byte_array_size, "raw string exceeds the byte limit");
   detail::pack_unsigned_int(stream, static_cast<std::uint32_t>(value.size()));
   if (!value.empty()) {
      stream.write(value.data(), value.size());
   }
}

template <typename Stream> void unpack(Stream& stream, std::string& value) {
   const auto size = detail::unpack_unsigned_int(stream);
   detail::require(size <= max_byte_array_size, "raw string exceeds the byte limit");
   value.resize(size);
   if (!value.empty()) {
      stream.read(value.data(), value.size());
   }
}

namespace detail {

template <typename Stream, typename Byte> void pack_byte_vector(Stream& stream, const std::vector<Byte>& value) {
   require(value.size() <= max_byte_array_size, "raw byte vector exceeds the byte limit");
   pack_unsigned_int(stream, static_cast<std::uint32_t>(value.size()));
   if (!value.empty()) {
      write_bytes(stream, std::as_bytes(std::span{value}));
   }
}

template <typename Stream, typename Byte> void unpack_byte_vector(Stream& stream, std::vector<Byte>& value) {
   const auto size = unpack_unsigned_int(stream);
   require(size <= max_byte_array_size, "raw byte vector exceeds the byte limit");
   value.resize(size);
   if (!value.empty()) {
      read_bytes(stream, std::as_writable_bytes(std::span{value}));
   }
}

} // namespace detail

template <typename Stream> void pack(Stream& stream, const std::vector<char>& value) {
   detail::pack_byte_vector(stream, value);
}

template <typename Stream> void unpack(Stream& stream, std::vector<char>& value) {
   detail::unpack_byte_vector(stream, value);
}

template <typename Stream> void pack(Stream& stream, const std::vector<std::uint8_t>& value) {
   detail::pack_byte_vector(stream, value);
}

template <typename Stream> void unpack(Stream& stream, std::vector<std::uint8_t>& value) {
   detail::unpack_byte_vector(stream, value);
}

template <typename Stream> void pack(Stream& stream, const std::vector<std::byte>& value) {
   detail::pack_byte_vector(stream, value);
}

template <typename Stream> void unpack(Stream& stream, std::vector<std::byte>& value) {
   detail::unpack_byte_vector(stream, value);
}

template <typename Stream, typename T> void pack(Stream& stream, const std::vector<T>& value) {
   detail::require(value.size() <= max_array_elements, "raw vector exceeds the element limit");
   detail::pack_unsigned_int(stream, static_cast<std::uint32_t>(value.size()));
   for (const auto& item : value) {
      pack(stream, item);
   }
}

template <typename Stream, typename T> void unpack(Stream& stream, std::vector<T>& value) {
   const auto size = detail::unpack_unsigned_int(stream);
   detail::require(size <= max_array_elements, "raw vector exceeds the element limit");
   value.resize(size);
   for (auto& item : value) {
      unpack(stream, item);
   }
}

template <typename Stream, typename T, typename Allocator>
void pack(Stream& stream, const std::deque<T, Allocator>& value) {
   detail::require(value.size() <= max_array_elements, "raw deque exceeds the element limit");
   detail::pack_unsigned_int(stream, static_cast<std::uint32_t>(value.size()));
   for (const auto& item : value) {
      pack(stream, item);
   }
}

template <typename Stream, typename T, typename Allocator>
void unpack(Stream& stream, std::deque<T, Allocator>& value) {
   const auto size = detail::unpack_unsigned_int(stream);
   detail::require(size <= max_array_elements, "raw deque exceeds the element limit");
   value.resize(size);
   for (auto& item : value) {
      unpack(stream, item);
   }
}

template <typename Stream, typename T, typename Allocator>
void pack(Stream& stream, const std::list<T, Allocator>& value) {
   detail::require(value.size() <= max_array_elements, "raw list exceeds the element limit");
   detail::pack_unsigned_int(stream, static_cast<std::uint32_t>(value.size()));
   for (const auto& item : value) {
      pack(stream, item);
   }
}

template <typename Stream, typename T, typename Allocator> void unpack(Stream& stream, std::list<T, Allocator>& value) {
   const auto size = detail::unpack_unsigned_int(stream);
   detail::require(size <= max_array_elements, "raw list exceeds the element limit");
   value.clear();
   for (auto index = std::uint32_t{0}; index < size; ++index) {
      auto item = T{};
      unpack(stream, item);
      value.emplace_back(std::move(item));
   }
}

template <typename Stream, typename Key, typename Compare, typename Allocator>
void pack(Stream& stream, const std::set<Key, Compare, Allocator>& value) {
   detail::require(value.size() <= max_array_elements, "raw set exceeds the element limit");
   detail::pack_unsigned_int(stream, static_cast<std::uint32_t>(value.size()));
   for (const auto& item : value) {
      pack(stream, item);
   }
}

template <typename Stream, typename Key, typename Compare, typename Allocator>
void unpack(Stream& stream, std::set<Key, Compare, Allocator>& value) {
   const auto size = detail::unpack_unsigned_int(stream);
   detail::require(size <= max_array_elements, "raw set exceeds the element limit");
   value.clear();
   for (auto index = std::uint32_t{0}; index < size; ++index) {
      auto item = Key{};
      unpack(stream, item);
      value.insert(std::move(item));
   }
}

template <typename Stream, typename Key, typename Value, typename Compare, typename Allocator>
void pack(Stream& stream, const std::map<Key, Value, Compare, Allocator>& value) {
   detail::require(value.size() <= max_array_elements, "raw map exceeds the element limit");
   detail::pack_unsigned_int(stream, static_cast<std::uint32_t>(value.size()));
   for (const auto& item : value) {
      pack(stream, item);
   }
}

template <typename Stream, typename Key, typename Value, typename Compare, typename Allocator>
void unpack(Stream& stream, std::map<Key, Value, Compare, Allocator>& value) {
   const auto size = detail::unpack_unsigned_int(stream);
   detail::require(size <= max_array_elements, "raw map exceeds the element limit");
   value.clear();
   for (auto index = std::uint32_t{0}; index < size; ++index) {
      auto item = std::pair<Key, Value>{};
      unpack(stream, item);
      value.insert(std::move(item));
   }
}

template <typename Stream, typename T> void pack(Stream& stream, const std::optional<T>& value) {
   pack(stream, value.has_value());
   if (value) {
      pack(stream, *value);
   }
}

template <typename Stream, typename T> void unpack(Stream& stream, std::optional<T>& value) {
   auto present = false;
   unpack(stream, present);
   if (!present) {
      value.reset();
      return;
   }
   value.emplace();
   unpack(stream, *value);
}

template <typename Stream, typename First, typename Second>
void pack(Stream& stream, const std::pair<First, Second>& value) {
   pack(stream, value.first);
   pack(stream, value.second);
}

template <typename Stream, typename First, typename Second>
void unpack(Stream& stream, std::pair<First, Second>& value) {
   unpack(stream, value.first);
   unpack(stream, value.second);
}

template <typename Stream, typename... T> void pack(Stream& stream, const std::tuple<T...>& value) {
   std::apply([&](const auto&... item) { (pack(stream, item), ...); }, value);
}

template <typename Stream, typename... T> void unpack(Stream& stream, std::tuple<T...>& value) {
   std::apply([&](auto&... item) { (unpack(stream, item), ...); }, value);
}

template <typename Stream, typename T, std::size_t Size> void pack(Stream& stream, const std::array<T, Size>& value) {
   static_assert(Size <= max_array_elements, "raw array exceeds the element limit");
   if constexpr (std::is_scalar_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>) {
      detail::write_bytes(stream, std::as_bytes(std::span{value}));
   } else {
      for (const auto& item : value) {
         pack(stream, item);
      }
   }
}

template <typename Stream, typename T, std::size_t Size> void unpack(Stream& stream, std::array<T, Size>& value) {
   static_assert(Size <= max_array_elements, "raw array exceeds the element limit");
   if constexpr (std::is_scalar_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>) {
      detail::read_bytes(stream, std::as_writable_bytes(std::span{value}));
   } else {
      for (auto& item : value) {
         unpack(stream, item);
      }
   }
}

template <typename Stream, typename... T> void pack(Stream& stream, const std::variant<T...>& value) {
   detail::pack_unsigned_int(stream, static_cast<std::uint32_t>(value.index()));
   std::visit([&](const auto& selected) { pack(stream, selected); }, value);
}

template <typename Stream, typename... T> void unpack(Stream& stream, std::variant<T...>& value) {
   const auto selected = detail::unpack_unsigned_int(stream);
   detail::select_variant(value, selected);
   std::visit([&](auto& item) { unpack(stream, item); }, value);
}

template <typename Stream, typename First, typename... Rest>
void pack(Stream& stream, const First& first, const Rest&... rest) {
   pack(stream, first);
   (pack(stream, rest), ...);
}

template <typename Stream, typename First, typename... Rest> void unpack(Stream& stream, First& first, Rest&... rest) {
   unpack(stream, first);
   (unpack(stream, rest), ...);
}

template <typename T> std::size_t pack_size(const T& value) {
   auto stream = datastream<std::size_t>{};
   pack(stream, value);
   return stream.tellp();
}

template <typename T> bytes pack(const T& value) {
   auto result = bytes(pack_size(value));
   if (!result.empty()) {
      auto stream = datastream<std::uint8_t*>{result.data(), result.size()};
      pack(stream, value);
   }
   return result;
}

template <typename T> void pack(std::vector<std::uint8_t>& output, const T& value) {
   output = pack(value);
}

template <typename T, typename... Rest> bytes pack(const T& value, const Rest&... rest) {
   auto sizing = datastream<std::size_t>{};
   pack(sizing, value, rest...);
   auto result = bytes(sizing.tellp());
   if (!result.empty()) {
      auto stream = datastream<std::uint8_t*>{result.data(), result.size()};
      pack(stream, value, rest...);
   }
   return result;
}

template <typename T> T unpack(std::span<const std::uint8_t> input) {
   auto value = T{};
   auto stream = datastream<const std::uint8_t*>{input.data(), input.size()};
   unpack(stream, value);
   return value;
}

template <typename T> void unpack(std::span<const std::uint8_t> input, T& value) {
   auto stream = datastream<const std::uint8_t*>{input.data(), input.size()};
   unpack(stream, value);
}

template <typename T> T unpack_exact(std::span<const std::uint8_t> input) {
   auto value = T{};
   auto stream = datastream<const std::uint8_t*>{input.data(), input.size()};
   unpack(stream, value);
   detail::require(stream.remaining() == 0U, "raw input contains trailing bytes");
   return value;
}

template <typename T> T unpack(const std::vector<std::uint8_t>& input) {
   return unpack<T>(std::span<const std::uint8_t>{input});
}

template <typename T> void unpack(const std::vector<std::uint8_t>& input, T& value) {
   unpack(std::span<const std::uint8_t>{input}, value);
}

template <typename T> void pack(std::uint8_t* destination, std::uint32_t size, const T& value) {
   auto stream = datastream<std::uint8_t*>{destination, size};
   pack(stream, value);
}

template <typename T> T unpack(const std::uint8_t* data, std::uint32_t size) {
   return unpack<T>(std::span<const std::uint8_t>{data, size});
}

template <typename T> void unpack(const std::uint8_t* data, std::uint32_t size, T& value) {
   unpack(std::span<const std::uint8_t>{data, size}, value);
}

} // namespace forge::raw
