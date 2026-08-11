module;

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

export module forge.crypto.bls.values;

import forge.raw.codec;

export namespace forge::crypto::bls {

using scalar = std::array<char, 32>;
using field_element = std::array<char, 48>;
using wide_scalar = std::array<char, 64>;
using field_element2 = std::array<field_element, 2>;
using g1 = std::array<char, 96>;
using g2 = std::array<char, 192>;
using gt = std::array<char, 576>;

class public_key {
 public:
   using data_type = std::array<std::uint8_t, 96>;
   static constexpr std::size_t size_bytes = data_type{}.size();

   constexpr public_key() = default;

   constexpr explicit public_key(std::span<const std::uint8_t, size_bytes> bytes) noexcept {
      for (auto index = std::size_t{}; index < size_bytes; ++index) {
         _bytes[index] = bytes[index];
      }
   }

   [[nodiscard]] constexpr std::span<const std::uint8_t, size_bytes> bytes() const& noexcept {
      return _bytes;
   }

   [[nodiscard]] constexpr std::span<const std::uint8_t, size_bytes> bytes() const&& noexcept = delete;

   [[nodiscard]] constexpr std::size_t size() const noexcept {
      return size_bytes;
   }

   [[nodiscard]] constexpr const data_type& serialize() const& noexcept {
      return _bytes;
   }

   [[nodiscard]] constexpr const data_type& serialize() const&& noexcept = delete;

   friend constexpr bool operator==(const public_key&, const public_key&) = default;
   friend constexpr auto operator<=>(const public_key&, const public_key&) = default;

 private:
   data_type _bytes{};

   template <typename Stream> friend void raw_pack(Stream&, const public_key&);
   template <typename Stream> friend void raw_unpack(Stream&, public_key&);
};

class signature {
 public:
   using data_type = std::array<std::uint8_t, 192>;
   static constexpr std::size_t size_bytes = data_type{}.size();

   constexpr signature() = default;

   constexpr explicit signature(std::span<const std::uint8_t, size_bytes> bytes) noexcept {
      for (auto index = std::size_t{}; index < size_bytes; ++index) {
         _bytes[index] = bytes[index];
      }
   }

   [[nodiscard]] constexpr std::span<const std::uint8_t, size_bytes> bytes() const& noexcept {
      return _bytes;
   }

   [[nodiscard]] constexpr std::span<const std::uint8_t, size_bytes> bytes() const&& noexcept = delete;

   [[nodiscard]] constexpr std::size_t size() const noexcept {
      return size_bytes;
   }

   [[nodiscard]] constexpr const data_type& serialize() const& noexcept {
      return _bytes;
   }

   [[nodiscard]] constexpr const data_type& serialize() const&& noexcept = delete;

   friend constexpr bool operator==(const signature&, const signature&) = default;
   friend constexpr auto operator<=>(const signature&, const signature&) = default;

 private:
   data_type _bytes{};

   template <typename Stream> friend void raw_pack(Stream&, const signature&);
   template <typename Stream> friend void raw_unpack(Stream&, signature&);
};

class aggregate_signature {
 public:
   using data_type = std::array<std::uint8_t, 192>;
   static constexpr std::size_t size_bytes = data_type{}.size();

   constexpr aggregate_signature() = default;

   constexpr explicit aggregate_signature(std::span<const std::uint8_t, size_bytes> bytes) noexcept {
      for (auto index = std::size_t{}; index < size_bytes; ++index) {
         _bytes[index] = bytes[index];
      }
   }

   [[nodiscard]] constexpr std::span<const std::uint8_t, size_bytes> bytes() const& noexcept {
      return _bytes;
   }

   [[nodiscard]] constexpr std::span<const std::uint8_t, size_bytes> bytes() const&& noexcept = delete;

   [[nodiscard]] constexpr std::size_t size() const noexcept {
      return size_bytes;
   }

   [[nodiscard]] constexpr const data_type& serialize() const& noexcept {
      return _bytes;
   }

   [[nodiscard]] constexpr const data_type& serialize() const&& noexcept = delete;

   friend constexpr bool operator==(const aggregate_signature&, const aggregate_signature&) = default;
   friend constexpr auto operator<=>(const aggregate_signature&, const aggregate_signature&) = default;

 private:
   data_type _bytes{};

   template <typename Stream> friend void raw_pack(Stream&, const aggregate_signature&);
   template <typename Stream> friend void raw_unpack(Stream&, aggregate_signature&);
};

template <typename Stream> void raw_pack(Stream& stream, const public_key& value) {
   forge::raw::pack(stream, forge::unsigned_int{public_key::size_bytes});
   stream.write(reinterpret_cast<const char*>(value._bytes.data()), value._bytes.size());
}

template <typename Stream> void raw_unpack(Stream& stream, public_key& value) {
   auto size = forge::unsigned_int{};
   forge::raw::unpack(stream, size);
   forge::raw::detail::require(size.value == public_key::size_bytes, "BLS public key must contain exactly 96 bytes");
   stream.read(reinterpret_cast<char*>(value._bytes.data()), value._bytes.size());
}

template <typename Stream> void raw_pack(Stream& stream, const signature& value) {
   forge::raw::pack(stream, forge::unsigned_int{signature::size_bytes});
   stream.write(reinterpret_cast<const char*>(value._bytes.data()), value._bytes.size());
}

template <typename Stream> void raw_unpack(Stream& stream, signature& value) {
   auto size = forge::unsigned_int{};
   forge::raw::unpack(stream, size);
   forge::raw::detail::require(size.value == signature::size_bytes, "BLS signature must contain exactly 192 bytes");
   stream.read(reinterpret_cast<char*>(value._bytes.data()), value._bytes.size());
}

template <typename Stream> void raw_pack(Stream& stream, const aggregate_signature& value) {
   forge::raw::pack(stream, forge::unsigned_int{aggregate_signature::size_bytes});
   stream.write(reinterpret_cast<const char*>(value._bytes.data()), value._bytes.size());
}

template <typename Stream> void raw_unpack(Stream& stream, aggregate_signature& value) {
   auto size = forge::unsigned_int{};
   forge::raw::unpack(stream, size);
   forge::raw::detail::require(size.value == aggregate_signature::size_bytes,
                               "BLS aggregate signature must contain exactly 192 bytes");
   stream.read(reinterpret_cast<char*>(value._bytes.data()), value._bytes.size());
}

} // namespace forge::crypto::bls
