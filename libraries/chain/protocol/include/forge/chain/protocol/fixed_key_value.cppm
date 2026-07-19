module;

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#if !defined(FORGE_CONTRACT_GUEST)
#include <stdexcept>
#endif
#include <type_traits>

export module forge.chain.protocol.fixed_key:value;

import forge.chain.protocol.values;
import forge.raw.codec;
#if !defined(FORGE_CONTRACT_GUEST)
import forge.raw.exceptions;
#endif

namespace forge::chain::protocol::detail {

template <typename Word>
concept fixed_key_word =
    !std::same_as<std::remove_cv_t<Word>, bool> &&
    (std::unsigned_integral<std::remove_cv_t<Word>> || std::same_as<std::remove_cv_t<Word>, uint128_t>);

template <std::size_t Size> [[nodiscard]] constexpr std::size_t fixed_key_num_words() noexcept {
   return (Size + sizeof(uint128_t) - 1U) / sizeof(uint128_t);
}

template <std::size_t Size> [[nodiscard]] constexpr std::size_t fixed_key_padded_bytes() noexcept {
   return fixed_key_num_words<Size>() * sizeof(uint128_t) - Size;
}

template <std::size_t Size, fixed_key_word Word, std::size_t WordCount>
   requires(sizeof(Word) <= sizeof(uint128_t) && sizeof(uint128_t) % sizeof(Word) == 0U &&
            WordCount * sizeof(Word) <= Size)
[[nodiscard]] constexpr auto pack_fixed_key_words(const std::array<Word, WordCount>& input) noexcept {
   auto output = std::array<uint128_t, fixed_key_num_words<Size>()>{};
   auto output_index = std::size_t{};
   auto packed = uint128_t{};
   constexpr auto shift = static_cast<unsigned>(8U * sizeof(Word));
   constexpr auto words_per_output = sizeof(uint128_t) / sizeof(Word);
   auto words_left = words_per_output;

   for (const auto word : input) {
      if constexpr (words_per_output == 1U) {
         output[output_index++] = static_cast<uint128_t>(word);
      } else {
         if (words_left > 1U) {
            packed |= static_cast<uint128_t>(word);
            packed <<= shift;
            --words_left;
            continue;
         }
         packed |= static_cast<uint128_t>(word);
         output[output_index++] = packed;
         packed = 0U;
         words_left = words_per_output;
      }
   }

   if (words_left != words_per_output) {
      if (words_left > 1U) {
         // CDT and Spring count residual padding in bytes rather than Word-sized lanes. Preserve that observable
         // layout because it defines raw fixed_key bytes and secondary-index keys for partial word sequences.
         packed <<= static_cast<unsigned>(8U * (words_left - 1U));
      }
      output[output_index] = packed;
   }
   return output;
}

template <std::size_t Size>
[[nodiscard]] constexpr auto
extract_fixed_key_bytes(const std::array<uint128_t, fixed_key_num_words<Size>()>& words) noexcept {
   auto output = std::array<std::uint8_t, Size>{};
   auto output_index = std::size_t{};
   for (auto word_index = std::size_t{}; word_index < words.size(); ++word_index) {
      auto bytes_left = sizeof(uint128_t);
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

[[noreturn]] inline void fail_fixed_key_read() {
   forge::raw::detail::fail_codec("chain fixed key raw payload is truncated");
}

template <typename Stream> void read_fixed_key_bytes(Stream& stream, char* data, std::size_t size) {
#if defined(FORGE_CONTRACT_GUEST)
   if constexpr (std::same_as<decltype(stream.read(data, size)), bool>) {
      if (!stream.read(data, size)) {
         fail_fixed_key_read();
      }
   } else if (static_cast<std::size_t>(stream.read(data, size)) != size) {
      fail_fixed_key_read();
   }
#else
   try {
      if constexpr (std::same_as<decltype(stream.read(data, size)), bool>) {
         if (!stream.read(data, size)) {
            fail_fixed_key_read();
         }
      } else if (static_cast<std::size_t>(stream.read(data, size)) != size) {
         fail_fixed_key_read();
      }
   } catch (const forge::raw::exceptions::range_error&) {
      fail_fixed_key_read();
   } catch (const std::out_of_range&) {
      fail_fixed_key_read();
   }
#endif
}

} // namespace forge::chain::protocol::detail

export namespace forge::chain::protocol {

template <std::size_t Size> class fixed_key {
 public:
   using word_type = uint128_t;

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

   template <detail::fixed_key_word Word, std::size_t WordCount>
      requires(sizeof(Word) < sizeof(word_type) && sizeof(word_type) % sizeof(Word) == 0U &&
               WordCount * sizeof(Word) <= Size)
   explicit constexpr fixed_key(const std::array<Word, WordCount>& words)
       : words_{detail::pack_fixed_key_words<Size>(words)} {}

   template <detail::fixed_key_word First, std::same_as<First>... Rest>
      requires(sizeof(First) <= sizeof(word_type) && sizeof(word_type) % sizeof(First) == 0U &&
               (1U + sizeof...(Rest)) * sizeof(First) <= Size)
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

   [[nodiscard]] constexpr bool operator==(const fixed_key& other) const noexcept {
      for (auto index = std::size_t{}; index < words_.size(); ++index) {
         if (words_[index] != other.words_[index]) {
            return false;
         }
      }
      return true;
   }

   [[nodiscard]] constexpr std::strong_ordering operator<=>(const fixed_key& other) const noexcept {
      for (auto index = std::size_t{}; index < words_.size(); ++index) {
         if (words_[index] < other.words_[index]) {
            return std::strong_ordering::less;
         }
         if (words_[index] > other.words_[index]) {
            return std::strong_ordering::greater;
         }
      }
      return std::strong_ordering::equal;
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

} // namespace forge::chain::protocol
