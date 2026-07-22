module;

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#if !defined(FORGE_CONTRACT_GUEST)
#include <memory>
#include <string>
#endif

export module forge.crypto.digest.ripemd160:value;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.crypto.digest.packhash;
import forge.crypto.digest.sha256;
import forge.crypto.digest.sha512;
#endif

export namespace forge::crypto::digest {

class ripemd160
#if !defined(FORGE_CONTRACT_GUEST)
    : public add_packhash_to_hash<ripemd160>
#endif
{
 public:
   static constexpr std::size_t byte_size = 20U;

   constexpr ripemd160() = default;

#if !defined(FORGE_CONTRACT_GUEST)
   explicit ripemd160(const std::string& hex);

   [[nodiscard]] std::string str() const;
   explicit operator std::string() const;

   static ripemd160 hash(const sha512& value);
   static ripemd160 hash(const sha256& value);
   static ripemd160 hash(const char* data, std::uint32_t size);
   static ripemd160 hash(const std::string& value);

   template <typename T> static ripemd160 hash(const T& value) {
      return packhash(value);
   }

   class encoder {
    public:
      encoder();
      ~encoder();

      void write(const char* data, std::uint32_t size);
      void put(char value) {
         write(&value, 1U);
      }
      void reset();
      ripemd160 result();

    private:
      struct impl;
      std::unique_ptr<impl> my;
   };

   friend ripemd160 operator<<(const ripemd160& value, std::uint32_t bits);
   friend ripemd160 operator^(const ripemd160& left, const ripemd160& right);
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

   [[nodiscard]] constexpr std::array<std::uint8_t, byte_size> extract_as_byte_array() const noexcept {
      auto result = std::array<std::uint8_t, byte_size>{};
      const auto* bytes = reinterpret_cast<const std::uint8_t*>(_hash);
      for (auto index = std::size_t{}; index < result.size(); ++index) {
         result[index] = bytes[index];
      }
      return result;
   }

   friend constexpr bool operator==(const ripemd160&, const ripemd160&) = default;

   [[nodiscard]] constexpr std::strong_ordering operator<=>(const ripemd160& other) const noexcept {
      const auto left = extract_as_byte_array();
      const auto right = other.extract_as_byte_array();
      return left <=> right;
   }

   template <typename Stream> friend Stream& operator<<(Stream& stream, const ripemd160& value) {
      stream.write(value.data(), data_size());
      return stream;
   }

   template <typename Stream> friend Stream& operator>>(Stream& stream, ripemd160& value) {
      stream.read(value.data(), data_size());
      return stream;
   }

   std::uint32_t _hash[5]{};
};

using uint160_t = ripemd160;
using uint160 = ripemd160;

} // namespace forge::crypto
