module;

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#if !defined(FORGE_CONTRACT_GUEST)
#include <memory>
#include <string>
#endif

export module forge.crypto.digest.sha512:value;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.crypto.digest.packhash;
#endif

export namespace forge::crypto::digest {

class sha512
#if !defined(FORGE_CONTRACT_GUEST)
    : public add_packhash_to_hash<sha512>
#endif
{
 public:
   static constexpr std::size_t byte_size = 64U;

   constexpr sha512() = default;

#if !defined(FORGE_CONTRACT_GUEST)
   explicit sha512(const std::string& hex);

   [[nodiscard]] std::string str() const;
   operator std::string() const;

   static sha512 hash(const char* data, std::uint32_t size);
   static sha512 hash(std::span<const std::uint8_t> data);
   static sha512 hash(const std::string& value);

   template <typename T> static sha512 hash(const T& value) {
      return packhash(value);
   }

   class encoder {
    public:
      encoder();
      ~encoder();

      void write(const char* data, std::uint32_t size);
      void write(std::span<const std::uint8_t> data);
      void put(char value) {
         write(&value, 1U);
      }
      void reset();
      sha512 result();

    private:
      struct impl;
      std::unique_ptr<impl> my;
   };

   friend sha512 operator<<(const sha512& value, std::uint32_t bits);
   friend sha512 operator^(const sha512& left, const sha512& right);
#endif

   [[nodiscard]] constexpr char* data() noexcept {
      return reinterpret_cast<char*>(_hash);
   }

   [[nodiscard]] constexpr const char* data() const noexcept {
      return reinterpret_cast<const char*>(_hash);
   }

   [[nodiscard]] static constexpr std::size_t data_size() noexcept {
      return byte_size;
   }

   [[nodiscard]] constexpr std::span<const std::uint8_t, byte_size> to_uint8_span() const noexcept {
      return std::span<const std::uint8_t, byte_size>{reinterpret_cast<const std::uint8_t*>(_hash), byte_size};
   }

   [[nodiscard]] constexpr std::array<std::uint8_t, byte_size> extract_as_byte_array() const noexcept {
      auto result = std::array<std::uint8_t, byte_size>{};
      for (auto index = std::size_t{}; index < result.size(); ++index) {
         result[index] = to_uint8_span()[index];
      }
      return result;
   }

   friend constexpr bool operator==(const sha512&, const sha512&) = default;

   [[nodiscard]] constexpr std::strong_ordering operator<=>(const sha512& other) const noexcept {
      for (auto index = std::size_t{}; index < data_size(); ++index) {
         const auto left = to_uint8_span()[index];
         const auto right = other.to_uint8_span()[index];
         if (left < right) {
            return std::strong_ordering::less;
         }
         if (left > right) {
            return std::strong_ordering::greater;
         }
      }
      return std::strong_ordering::equal;
   }

   template <typename Stream> friend Stream& operator<<(Stream& stream, const sha512& value) {
      stream.write(value.data(), data_size());
      return stream;
   }

   template <typename Stream> friend Stream& operator>>(Stream& stream, sha512& value) {
      stream.read(value.data(), data_size());
      return stream;
   }

   std::uint64_t _hash[8]{};
};

using uint512 = sha512;

} // namespace forge::crypto
