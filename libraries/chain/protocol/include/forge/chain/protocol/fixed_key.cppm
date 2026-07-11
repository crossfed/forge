module;

#include <forge/exceptions/macros.hpp>

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

export module forge.chain.protocol.fixed_key;

import forge.crypto.hex;
import forge.chain.protocol.types;
import forge.raw.exceptions;
import forge.raw.raw;
import forge.variant.exceptions;
import forge.variant.value;

namespace forge::chain::protocol::detail {

template <std::size_t Size> [[nodiscard]] constexpr std::size_t fixed_key_num_words() noexcept {
   return (Size + sizeof(forge::chain::protocol::uint128_t) - 1U) /
          sizeof(forge::chain::protocol::uint128_t);
}

template <std::size_t Size> [[nodiscard]] constexpr std::size_t fixed_key_padded_bytes() noexcept {
   return fixed_key_num_words<Size>() * sizeof(forge::chain::protocol::uint128_t) - Size;
}

template <std::size_t Size, std::unsigned_integral Word, std::size_t WordCount>
   requires(!std::same_as<Word, bool> && sizeof(Word) <= sizeof(forge::chain::protocol::uint128_t) &&
            sizeof(forge::chain::protocol::uint128_t) % sizeof(Word) == 0U &&
            WordCount * sizeof(Word) <= Size)
[[nodiscard]] constexpr auto pack_fixed_key_words(const std::array<Word, WordCount>& input) noexcept {
   auto output = std::array<forge::chain::protocol::uint128_t, fixed_key_num_words<Size>()>{};
   auto output_index = std::size_t{};
   auto packed = forge::chain::protocol::uint128_t{};
   constexpr auto shift = static_cast<unsigned>(8U * sizeof(Word));
   constexpr auto words_per_output = sizeof(forge::chain::protocol::uint128_t) / sizeof(Word);
   auto words_left = words_per_output;

   for (const auto word : input) {
      if constexpr (words_per_output == 1U) {
         output[output_index++] = static_cast<forge::chain::protocol::uint128_t>(word);
      } else {
         if (words_left > 1U) {
            packed |= static_cast<forge::chain::protocol::uint128_t>(word);
            packed <<= shift;
            --words_left;
            continue;
         }

         packed |= static_cast<forge::chain::protocol::uint128_t>(word);
         output[output_index++] = packed;
         packed = 0U;
         words_left = words_per_output;
      }
   }

   if (words_left != words_per_output) {
      if (words_left > 1U) {
         // Spring/CDT fixed_bytes uses byte shifts here, rather than Word-sized shifts.
         packed <<= static_cast<unsigned>(8U * (words_left - 1U));
      }
      output[output_index] = packed;
   }

   return output;
}

template <std::size_t Size>
[[nodiscard]] constexpr auto
extract_fixed_key_bytes(
   const std::array<forge::chain::protocol::uint128_t, fixed_key_num_words<Size>()>& words) noexcept {
   auto output = std::array<std::uint8_t, Size>{};
   auto output_index = std::size_t{};

   for (auto word_index = std::size_t{}; word_index < words.size(); ++word_index) {
      auto bytes_left = sizeof(forge::chain::protocol::uint128_t);
      auto word = words[word_index];
      if (word_index + 1U == words.size()) {
         bytes_left -= fixed_key_padded_bytes<Size>();
         word >>= static_cast<unsigned>(8U * fixed_key_padded_bytes<Size>());
      }

      for (auto byte_index = bytes_left; byte_index > 0U; --byte_index) {
         output[output_index + byte_index - 1U] = static_cast<std::uint8_t>(word & 0xffU);
         word >>= 8U;
      }
      output_index += bytes_left;
   }

   return output;
}

template <typename Stream> void read_fixed_key_bytes(Stream& stream, char* data, std::size_t size) {
   try {
      if constexpr (std::same_as<decltype(stream.read(data, size)), bool>) {
         if (!stream.read(data, size)) {
            FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error, "chain fixed key raw payload is truncated");
         }
      } else if (static_cast<std::size_t>(stream.read(data, size)) != size) {
         FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error, "chain fixed key raw payload is truncated");
      }
   } catch (const forge::raw::exceptions::range_error&) {
      FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error, "chain fixed key raw payload is truncated");
   } catch (const std::out_of_range&) {
      FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error, "chain fixed key raw payload is truncated");
   }
}

} // namespace forge::chain::protocol::detail

export namespace forge::chain::protocol {

template <std::size_t Size> class fixed_key {
 public:
   using word_type = forge::chain::protocol::uint128_t;

   [[nodiscard]] static constexpr std::size_t num_words() noexcept {
      return detail::fixed_key_num_words<Size>();
   }

   [[nodiscard]] static constexpr std::size_t padded_bytes() noexcept {
      return detail::fixed_key_padded_bytes<Size>();
   }

   fixed_key() = default;

   explicit constexpr fixed_key(const std::array<word_type, num_words()>& words) : words_{words} {}

   explicit constexpr fixed_key(const std::array<std::uint8_t, Size>& bytes)
       : words_{detail::pack_fixed_key_words<Size>(bytes)} {}

   template <std::unsigned_integral Word, std::size_t WordCount>
      requires(!std::same_as<Word, bool> && sizeof(Word) < sizeof(word_type) &&
               sizeof(word_type) % sizeof(Word) == 0U && WordCount * sizeof(Word) <= Size)
   explicit constexpr fixed_key(const std::array<Word, WordCount>& words)
       : words_{detail::pack_fixed_key_words<Size>(words)} {}

   template <std::unsigned_integral First, std::same_as<First>... Rest>
      requires(!std::same_as<First, bool> && sizeof(First) <= sizeof(word_type) &&
               sizeof(word_type) % sizeof(First) == 0U && (1U + sizeof...(Rest)) * sizeof(First) <= Size)
   [[nodiscard]] static constexpr fixed_key make_from_word_sequence(First first, Rest... rest) {
      auto result = fixed_key{};
      result.words_ = detail::pack_fixed_key_words<Size>(std::array<First, 1U + sizeof...(Rest)>{first, rest...});
      return result;
   }

   [[nodiscard]] const std::array<word_type, num_words()>& get_array() const noexcept {
      return words_;
   }

   [[nodiscard]] constexpr std::array<std::uint8_t, Size> extract_as_byte_array() const noexcept {
      return detail::extract_fixed_key_bytes<Size>(words_);
   }

   bool operator==(const fixed_key&) const = default;

   [[nodiscard]] std::strong_ordering operator<=>(const fixed_key& other) const noexcept {
      return words_ <=> other.words_;
   }

 private:
   std::array<word_type, num_words()> words_{};
};

template <typename Stream, std::size_t Size> Stream& operator<<(Stream& stream, const fixed_key<Size>& value) {
   const auto bytes = value.extract_as_byte_array();
   stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
   return stream;
}

template <typename Stream, std::size_t Size> Stream& operator>>(Stream& stream, fixed_key<Size>& value) {
   auto bytes = std::array<std::uint8_t, Size>{};
   detail::read_fixed_key_bytes(stream, reinterpret_cast<char*>(bytes.data()), bytes.size());
   value = fixed_key<Size>{bytes};
   return stream;
}

using key256 = fixed_key<32>;

} // namespace forge::chain::protocol

export namespace forge {

template <std::size_t Size>
void to_variant(const forge::chain::protocol::fixed_key<Size>& value, forge::variant& output) {
   const auto bytes = value.extract_as_byte_array();
   output = forge::crypto::to_hex(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

template <std::size_t Size>
void from_variant(const forge::variant& input, forge::chain::protocol::fixed_key<Size>& output) {
   const auto& text = input.get_string();
   if (text.size() != Size * 2U) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "chain fixed key has invalid hex length");
   }

   auto bytes = std::array<std::uint8_t, Size>{};
   try {
      if (forge::crypto::from_hex(text, bytes.data(), bytes.size()) != bytes.size()) {
         FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "chain fixed key has invalid hex");
      }
   } catch (const forge::crypto::hex::exceptions::invalid_character&) {
      FORGE_THROW_EXCEPTION(forge::variant_exceptions::decode_error, "chain fixed key has invalid hex");
   }
   output = forge::chain::protocol::fixed_key<Size>{bytes};
}

} // namespace forge
