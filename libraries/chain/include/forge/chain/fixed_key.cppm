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

export module forge.chain.fixed_key;

import forge.crypto.hex;
import forge.raw.exceptions;
import forge.raw.raw;
import forge.variant.exceptions;
import forge.variant.value;

namespace forge::chain::detail {

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

} // namespace forge::chain::detail

export namespace forge::chain {

template <std::size_t Size> class fixed_key {
   static_assert(Size > 0U, "forge::chain::fixed_key size must be greater than zero");

 public:
   using word_type = unsigned __int128;

   [[nodiscard]] static constexpr std::size_t num_words() noexcept {
      return (Size + sizeof(word_type) - 1U) / sizeof(word_type);
   }

   [[nodiscard]] static constexpr std::size_t padded_bytes() noexcept {
      return num_words() * sizeof(word_type) - Size;
   }

   fixed_key() = default;

   explicit fixed_key(const std::array<word_type, num_words()>& words) : words_{words} {
      mask_padding();
   }

   explicit fixed_key(const std::array<std::uint8_t, Size>& bytes) {
      assign_bytes(bytes);
   }

   template <std::unsigned_integral Word, std::size_t WordCount>
      requires(!std::same_as<Word, bool> && sizeof(Word) <= sizeof(word_type) && WordCount * sizeof(Word) <= Size)
   explicit fixed_key(const std::array<Word, WordCount>& words) {
      assign_words(words);
   }

   template <std::unsigned_integral First, std::same_as<First>... Rest>
      requires(!std::same_as<First, bool> && sizeof(First) <= sizeof(word_type) &&
               (1U + sizeof...(Rest)) * sizeof(First) <= Size)
   [[nodiscard]] static fixed_key make_from_word_sequence(First first, Rest... rest) {
      return fixed_key{std::array<First, 1U + sizeof...(Rest)>{first, rest...}};
   }

   [[nodiscard]] const std::array<word_type, num_words()>& get_array() const noexcept {
      return words_;
   }

   [[nodiscard]] std::array<std::uint8_t, Size> extract_as_byte_array() const noexcept {
      auto bytes = std::array<std::uint8_t, Size>{};
      auto output = std::size_t{};
      for (auto word_index = std::size_t{}; word_index < words_.size(); ++word_index) {
         const auto count = word_index + 1U == words_.size() ? sizeof(word_type) - padded_bytes() : sizeof(word_type);
         const auto word = words_[word_index];
         for (auto byte_index = count; byte_index > 0U; --byte_index) {
            const auto shift = static_cast<unsigned>((byte_index - 1U) * 8U);
            bytes[output++] = static_cast<std::uint8_t>((word >> shift) & word_type{0xffU});
         }
      }
      return bytes;
   }

   bool operator==(const fixed_key&) const = default;

   [[nodiscard]] std::strong_ordering operator<=>(const fixed_key& other) const noexcept {
      const auto lhs = extract_as_byte_array();
      const auto rhs = other.extract_as_byte_array();
      return lhs <=> rhs;
   }

   template <typename Stream> friend Stream& operator<<(Stream& stream, const fixed_key& value) {
      const auto bytes = value.extract_as_byte_array();
      stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      return stream;
   }

   template <typename Stream> friend Stream& operator>>(Stream& stream, fixed_key& value) {
      auto bytes = std::array<std::uint8_t, Size>{};
      detail::read_fixed_key_bytes(stream, reinterpret_cast<char*>(bytes.data()), bytes.size());
      value.assign_bytes(bytes);
      return stream;
   }

 private:
   template <std::unsigned_integral Word, std::size_t WordCount>
   void assign_words(const std::array<Word, WordCount>& words) noexcept {
      auto bytes = std::array<std::uint8_t, Size>{};
      auto output = std::size_t{};
      for (const auto word : words) {
         for (auto byte_index = sizeof(Word); byte_index > 0U; --byte_index) {
            const auto shift = static_cast<unsigned>((byte_index - 1U) * 8U);
            bytes[output++] = static_cast<std::uint8_t>((word >> shift) & static_cast<Word>(0xffU));
         }
      }
      assign_bytes(bytes);
   }

   void assign_bytes(const std::array<std::uint8_t, Size>& bytes) noexcept {
      words_.fill(0U);
      auto input = std::size_t{};
      for (auto word_index = std::size_t{}; word_index < words_.size(); ++word_index) {
         const auto count = word_index + 1U == words_.size() ? sizeof(word_type) - padded_bytes() : sizeof(word_type);
         auto word = word_type{};
         for (auto byte_index = std::size_t{}; byte_index < count; ++byte_index) {
            word = static_cast<word_type>((word << 8U) | bytes[input++]);
         }
         words_[word_index] = word;
      }
   }

   void mask_padding() noexcept {
      if constexpr (padded_bytes() != 0U) {
         constexpr auto bits = static_cast<unsigned>((sizeof(word_type) - padded_bytes()) * 8U);
         words_.back() &= (word_type{1U} << bits) - 1U;
      }
   }

   std::array<word_type, num_words()> words_{};
};

using key256 = fixed_key<32>;

} // namespace forge::chain

export namespace forge {

template <std::size_t Size> void to_variant(const forge::chain::fixed_key<Size>& value, forge::variant& output) {
   const auto bytes = value.extract_as_byte_array();
   output = forge::crypto::to_hex(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

template <std::size_t Size> void from_variant(const forge::variant& input, forge::chain::fixed_key<Size>& output) {
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
   output = forge::chain::fixed_key<Size>{bytes};
}

} // namespace forge
