module;

#include <array>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#if !defined(FORGE_CONTRACT_GUEST)
#include <memory>
#include <string>
#endif

export module forge.crypto.digest.sha256:value;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.crypto.digest.packhash;
#endif

export namespace forge::crypto::digest {

class sha256
#if !defined(FORGE_CONTRACT_GUEST)
    : public add_packhash_to_hash<sha256>
#endif
{
 public:
   static constexpr std::size_t byte_size = 32U;

   constexpr sha256() = default;

#if !defined(FORGE_CONTRACT_GUEST)
   explicit sha256(const std::string& hex);
   explicit sha256(const char* data, std::size_t size);
   explicit sha256(std::span<const std::uint8_t> data);

   [[nodiscard]] std::string str() const;
   operator std::string() const;

   static sha256 hash(const char* data, std::uint32_t size);
   static sha256 hash(std::span<const std::uint8_t> data);
   static sha256 hash(const std::string& value);
   static sha256 hash(const sha256& value);

   template <typename T> static sha256 hash(const T& value) {
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
      sha256 result();

    private:
      struct impl;
      std::unique_ptr<impl> my;
   };

   friend sha256 operator<<(const sha256& value, std::uint32_t bits);
   friend sha256 operator>>(const sha256& value, std::uint32_t bits);
   friend sha256 operator^(const sha256& left, const sha256& right);

   [[nodiscard]] std::uint16_t clz() const;
   [[nodiscard]] std::uint32_t approx_log_32() const;
   void set_to_inverse_approx_log_32(std::uint32_t value);
   static double inverse_approx_log_32_double(std::uint32_t value);
#endif

   [[nodiscard]] constexpr const char* data() const& noexcept {
      return reinterpret_cast<const char*>(_hash);
   }

   [[nodiscard]] constexpr char* data() & noexcept {
      return reinterpret_cast<char*>(_hash);
   }

   [[nodiscard]] constexpr char* data() && noexcept = delete;
   [[nodiscard]] constexpr const char* data() const&& noexcept = delete;

   [[nodiscard]] static constexpr std::size_t data_size() noexcept {
      return byte_size;
   }

   [[nodiscard]] constexpr std::span<const std::uint8_t, byte_size> to_uint8_span() const& noexcept {
      return std::span<const std::uint8_t, byte_size>{reinterpret_cast<const std::uint8_t*>(_hash), byte_size};
   }

   [[nodiscard]] constexpr std::span<const std::uint8_t, byte_size> to_uint8_span() && noexcept = delete;
   [[nodiscard]] constexpr std::span<const std::uint8_t, byte_size> to_uint8_span() const&& noexcept = delete;

   [[nodiscard]] constexpr std::array<std::uint8_t, byte_size> extract_as_byte_array() const noexcept {
      auto result = std::array<std::uint8_t, byte_size>{};
      for (auto index = std::size_t{}; index < result.size(); ++index) {
         result[index] = to_uint8_span()[index];
      }
      return result;
   }

   [[nodiscard]] constexpr bool empty() const noexcept {
      return (_hash[0] | _hash[1] | _hash[2] | _hash[3]) == 0U;
   }

   [[nodiscard]] constexpr std::uint32_t pop_count() const noexcept {
      return static_cast<std::uint32_t>(std::popcount(_hash[0]) + std::popcount(_hash[1]) + std::popcount(_hash[2]) +
                                        std::popcount(_hash[3]));
   }

   friend constexpr bool operator==(const sha256&, const sha256&) = default;

   [[nodiscard]] constexpr std::strong_ordering operator<=>(const sha256& other) const noexcept {
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

   template <typename Stream> friend Stream& operator<<(Stream& stream, const sha256& value) {
      stream.write(value.data(), data_size());
      return stream;
   }

   template <typename Stream> friend Stream& operator>>(Stream& stream, sha256& value) {
      stream.read(value.data(), data_size());
      return stream;
   }

   std::uint64_t _hash[4]{};
};

using uint256 = sha256;

[[nodiscard]] inline std::size_t hash_value(const sha256& value) noexcept {
   return static_cast<std::size_t>(value._hash[3]);
}

} // namespace forge::crypto::digest
