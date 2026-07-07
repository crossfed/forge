module;

#include <forge/exceptions/macros.hpp>

#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

export module forge.blobdb.ref;

import forge.blobdb.types;
import forge.crypto.hex;
import forge.crypto.sha256;
import forge.raw.raw;
import forge.variant.exceptions;
import forge.variant.value;

namespace forge::blobdb::detail {

[[nodiscard]] inline bool is_hex_digit(char value) noexcept {
   return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

inline void require_hex_text(std::string_view text, std::size_t expected_size) {
   if (text.size() != expected_size) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "blobdb ref digest has invalid hex length");
   }
   for (const auto value : text) {
      if (!is_hex_digit(value)) {
         FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "blobdb ref digest has invalid hex");
      }
   }
}

inline void require_variable_hex_text(std::string_view text) {
   if (text.empty() || (text.size() % 2U) != 0U) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "blobdb ref digest has invalid hex length");
   }
   for (const auto value : text) {
      if (!is_hex_digit(value)) {
         FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "blobdb ref digest has invalid hex");
      }
   }
}

[[nodiscard]] inline std::uint64_t parse_size(std::string_view text) {
   if (text.empty()) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "blobdb ref size is empty");
   }
   auto value = std::uint64_t{};
   const auto* begin = text.data();
   const auto* end = text.data() + text.size();
   const auto parsed = std::from_chars(begin, end, value);
   if (parsed.ec != std::errc{} || parsed.ptr != end) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "blobdb ref size is invalid");
   }
   return value;
}

} // namespace forge::blobdb::detail

export namespace forge::blobdb {

template <typename Digest>
struct digest_traits;

template <>
struct digest_traits<forge::crypto::sha256> {
   [[nodiscard]] static std::vector<std::byte> bytes(const forge::crypto::sha256& value) {
      const auto* begin = reinterpret_cast<const std::byte*>(value.data());
      return std::vector<std::byte>{begin, begin + value.data_size()};
   }

   [[nodiscard]] static forge::crypto::sha256 from_bytes(std::span<const std::byte> value) {
      if (value.size() != forge::crypto::sha256{}.data_size()) {
         FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "sha256 blobdb ref digest has invalid size");
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

template <>
struct digest_traits<digest> {
   [[nodiscard]] static std::vector<std::byte> bytes(const digest& value) {
      return value.bytes;
   }

   [[nodiscard]] static digest from_bytes(std::span<const std::byte> value) {
      return digest{std::vector<std::byte>{value.begin(), value.end()}};
   }

   [[nodiscard]] static std::string text(const digest& value) {
      return forge::crypto::to_hex(
         reinterpret_cast<const std::uint8_t*>(value.bytes.data()),
         static_cast<std::uint32_t>(value.bytes.size()));
   }

   [[nodiscard]] static digest from_text(std::string_view value) {
      detail::require_variable_hex_text(value);
      auto decoded = std::vector<std::uint8_t>(value.size() / 2U);
      forge::crypto::from_hex(std::string{value}, decoded.data(), decoded.size());
      auto bytes = std::vector<std::byte>{};
      bytes.reserve(decoded.size());
      for (const auto byte : decoded) {
         bytes.push_back(static_cast<std::byte>(byte));
      }
      return digest{std::move(bytes)};
   }
};

template <typename Digest = forge::crypto::sha256>
struct ref {
   Digest digest;
   std::uint64_t size = 0;

   bool operator==(const ref&) const = default;
   auto operator<=>(const ref&) const = default;

   template <typename Stream>
   friend Stream& operator<<(Stream& stream, const ref& value) {
      forge::raw::pack(stream, value.digest);
      forge::raw::pack(stream, value.size);
      return stream;
   }

   template <typename Stream>
   friend Stream& operator>>(Stream& stream, ref& value) {
      forge::raw::unpack(stream, value.digest);
      forge::raw::unpack(stream, value.size);
      return stream;
   }
};

using sha256_ref = ref<forge::crypto::sha256>;

} // namespace forge::blobdb

export namespace forge {

template <typename Digest>
void to_variant(const forge::blobdb::ref<Digest>& value, forge::variant& output) {
   output = forge::blobdb::digest_traits<Digest>::text(value.digest) + ":" + std::to_string(value.size);
}

template <typename Digest>
void from_variant(const forge::variant& input, forge::blobdb::ref<Digest>& output) {
   const auto text = input.get_string();
   const auto split = text.find(':');
   if (split == std::string::npos || split == 0U || split + 1U >= text.size() || text.find(':', split + 1U) != std::string::npos) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "blobdb ref must be formatted as <digest>:<size>");
   }
   output.digest = forge::blobdb::digest_traits<Digest>::from_text(std::string_view{text}.substr(0U, split));
   output.size = forge::blobdb::detail::parse_size(std::string_view{text}.substr(split + 1U));
}

} // namespace forge
