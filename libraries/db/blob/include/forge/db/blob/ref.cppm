module;

#include <forge/exceptions/macros.hpp>

#include <charconv>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

export module forge.db.blob.ref;

import forge.db.blob.types;
import forge.codec.hex;
import forge.crypto.sha256;
import forge.raw.exceptions;
import forge.raw.raw;
import forge.variant.exceptions;
import forge.variant.value;

export namespace forge::db::blob {
template <typename Digest> struct digest_traits;
}

namespace forge::db::blob::detail {

[[nodiscard]] inline bool is_hex_digit(char value) noexcept {
   return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

inline void require_hex_text(std::string_view text, std::size_t expected_size) {
   if (text.size() != expected_size) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "db blob ref digest has invalid hex length");
   }
   for (const auto value : text) {
      if (!is_hex_digit(value)) {
         FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "db blob ref digest has invalid hex");
      }
   }
}

[[nodiscard]] inline std::uint64_t parse_size(std::string_view text) {
   if (text.empty()) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "db blob ref size is empty");
   }
   auto value = std::uint64_t{};
   const auto* begin = text.data();
   const auto* end = text.data() + text.size();
   const auto parsed = std::from_chars(begin, end, value);
   if (parsed.ec != std::errc{} || parsed.ptr != end) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "db blob ref size is invalid");
   }
   return value;
}

template <typename Digest>
concept fixed_size_digest = requires {
   { digest_traits<Digest>::byte_size } -> std::convertible_to<std::size_t>;
};

template <typename Digest>
concept max_size_digest = requires {
   { digest_traits<Digest>::max_byte_size } -> std::convertible_to<std::size_t>;
};

template <typename Digest> [[nodiscard]] constexpr std::size_t max_digest_byte_size() {
   if constexpr (fixed_size_digest<Digest>) {
      return digest_traits<Digest>::byte_size;
   } else if constexpr (max_size_digest<Digest>) {
      return digest_traits<Digest>::max_byte_size;
   } else {
      return std::size_t{4096U};
   }
}

template <typename Digest> void require_digest_byte_size(std::size_t size) {
   if (size > max_digest_byte_size<Digest>()) {
      FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error, "db blob ref digest raw size exceeds limit");
   }
}

template <typename Stream> void read_exact(Stream& stream, char* data, std::size_t size) {
   if (size == 0U) {
      return;
   }
   try {
      if constexpr (std::same_as<decltype(stream.read(data, size)), bool>) {
         if (!stream.read(data, size)) {
            FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error, "db blob ref raw payload is truncated");
         }
      } else {
         const auto read = stream.read(data, size);
         if (static_cast<std::size_t>(read) != size) {
            FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error, "db blob ref raw payload is truncated");
         }
      }
   } catch (const forge::raw::exceptions::range_error&) {
      FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error, "db blob ref raw payload is truncated");
   } catch (const std::out_of_range&) {
      FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error, "db blob ref raw payload is truncated");
   }
}

template <typename Stream, typename Value> void read_scalar(Stream& stream, Value& value) {
   static_assert(std::is_trivially_copyable_v<Value>);
   read_exact(stream, reinterpret_cast<char*>(&value), sizeof(value));
}

} // namespace forge::db::blob::detail

export namespace forge::db::blob {

template <typename Digest> struct hash;

template <> struct hash<forge::crypto::sha256> {
   [[nodiscard]] forge::crypto::sha256 operator()(std::span<const std::byte> value) const {
      return forge::crypto::sha256::hash(
          std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
   }
};

template <> struct digest_traits<forge::crypto::sha256> {
   static constexpr auto algorithm = std::string_view{"sha256"};
   static constexpr auto byte_size = std::size_t{32U};

   [[nodiscard]] static std::vector<std::byte> to_bytes(const forge::crypto::sha256& value) {
      const auto* begin = reinterpret_cast<const std::byte*>(value.data());
      return std::vector<std::byte>{begin, begin + value.data_size()};
   }

   [[nodiscard]] static forge::crypto::sha256 from_bytes(std::span<const std::byte> value) {
      if (value.size() != forge::crypto::sha256{}.data_size()) {
         FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "sha256 db blob ref digest has invalid size");
      }
      return forge::crypto::sha256{reinterpret_cast<const char*>(value.data()), value.size()};
   }

   [[nodiscard]] static std::string text(const forge::crypto::sha256& value) {
      return value.str();
   }

   [[nodiscard]] static forge::crypto::sha256 from_text(std::string_view value) {
      detail::require_hex_text(value, forge::crypto::sha256{}.data_size() * 2U);
      return forge::crypto::sha256{std::string{value}};
   }
};

template <typename Digest>
concept digest_algorithm = requires(Digest value, std::span<const std::byte> bytes, std::string_view text) {
   { hash<Digest>{}(bytes) } -> std::same_as<Digest>;
   { digest_traits<Digest>::algorithm } -> std::convertible_to<std::string_view>;
   { digest_traits<Digest>::to_bytes(value) } -> std::same_as<std::vector<std::byte>>;
   { digest_traits<Digest>::from_bytes(bytes) } -> std::same_as<Digest>;
   { digest_traits<Digest>::text(value) } -> std::same_as<std::string>;
   { digest_traits<Digest>::from_text(text) } -> std::same_as<Digest>;
};

template <typename Digest = digest> struct ref {
   Digest digest;
   std::uint64_t size = 0;

   bool operator==(const ref&) const = default;
   auto operator<=>(const ref&) const = default;

   template <typename Stream> friend Stream& operator<<(Stream& stream, const ref& value) {
      const auto digest_bytes = digest_traits<Digest>::to_bytes(value.digest);
      if constexpr (detail::fixed_size_digest<Digest>) {
         if (digest_bytes.size() != digest_traits<Digest>::byte_size) {
            FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error, "db blob ref digest has invalid fixed size");
         }
      } else {
         detail::require_digest_byte_size<Digest>(digest_bytes.size());
         forge::raw::pack(stream, static_cast<std::uint32_t>(digest_bytes.size()));
      }
      if (!digest_bytes.empty()) {
         stream.write(reinterpret_cast<const char*>(digest_bytes.data()), digest_bytes.size());
      }
      forge::raw::pack(stream, value.size);
      return stream;
   }

   template <typename Stream> friend Stream& operator>>(Stream& stream, ref& value) {
      auto digest_size = std::size_t{};
      if constexpr (detail::fixed_size_digest<Digest>) {
         digest_size = digest_traits<Digest>::byte_size;
      } else {
         auto encoded_size = std::uint32_t{};
         detail::read_scalar(stream, encoded_size);
         detail::require_digest_byte_size<Digest>(encoded_size);
         digest_size = encoded_size;
      }
      auto digest_bytes = std::vector<std::byte>(digest_size);
      if (!digest_bytes.empty()) {
         detail::read_exact(stream, reinterpret_cast<char*>(digest_bytes.data()), digest_bytes.size());
      }
      value.digest = digest_traits<Digest>::from_bytes(digest_bytes);
      detail::read_scalar(stream, value.size);
      return stream;
   }
};

} // namespace forge::db::blob

export namespace forge {

template <typename Digest> void to_variant(const forge::db::blob::ref<Digest>& value, forge::variant& output) {
   output = forge::db::blob::digest_traits<Digest>::text(value.digest) + ":" + std::to_string(value.size);
}

template <typename Digest> void from_variant(const forge::variant& input, forge::db::blob::ref<Digest>& output) {
   const auto text = input.get_string();
   const auto split = text.find(':');
   if (split == std::string::npos || split == 0U || split + 1U >= text.size() ||
       text.find(':', split + 1U) != std::string::npos) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error,
                            "db blob ref must be formatted as <digest>:<size>");
   }
   output.digest = forge::db::blob::digest_traits<Digest>::from_text(std::string_view{text}.substr(0U, split));
   output.size = forge::db::blob::detail::parse_size(std::string_view{text}.substr(split + 1U));
}

} // namespace forge
