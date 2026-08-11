module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module forge.crypto.bls.serialization;

import forge.codec.base64;
import forge.crypto.digest.ripemd160;
import forge.variant.value;

namespace forge::crypto::bls {

namespace {

constexpr auto public_key_prefix = std::string_view{"PUB_BLS_"};
constexpr auto signature_prefix = std::string_view{"SIG_BLS_"};
constexpr auto checksum_size = std::size_t{4};

template <std::size_t Size>
[[nodiscard]] std::array<std::uint8_t, Size> decode_checked(std::string_view text, std::string_view prefix) {
   if (!text.starts_with(prefix)) {
      FORGE_THROW_EXCEPTION(exceptions::parse_error, "BLS text has an invalid prefix");
   }

   auto decoded = std::vector<std::uint8_t>{};
   try {
      decoded = forge::codec::base64::decode(
          text.substr(prefix.size()),
          {.characters = forge::codec::base64::alphabet::url, .pad = forge::codec::base64::padding_policy::allow});
   } catch (const std::exception&) {
      FORGE_THROW_EXCEPTION(exceptions::parse_error, "BLS text has an invalid base64url payload");
   }

   if (decoded.size() != Size + checksum_size) {
      FORGE_THROW_EXCEPTION(exceptions::parse_error, "BLS text has an invalid payload length");
   }

   auto result = std::array<std::uint8_t, Size>{};
   std::copy_n(decoded.begin(), Size, result.begin());

   auto encoder = forge::crypto::digest::ripemd160::encoder{};
   encoder.write(reinterpret_cast<const char*>(result.data()), static_cast<std::uint32_t>(result.size()));
   const auto checksum = encoder.result().extract_as_byte_array();
   if (!std::equal(checksum.begin(), checksum.begin() + checksum_size, decoded.begin() + Size)) {
      FORGE_THROW_EXCEPTION(exceptions::parse_error, "BLS text checksum mismatch");
   }

   return result;
}

[[nodiscard]] std::string encode_checked(std::span<const std::uint8_t> value, std::string_view prefix) {
   auto encoder = forge::crypto::digest::ripemd160::encoder{};
   encoder.write(reinterpret_cast<const char*>(value.data()), static_cast<std::uint32_t>(value.size()));
   const auto checksum = encoder.result().extract_as_byte_array();

   auto payload = std::vector<std::uint8_t>{};
   payload.reserve(value.size() + checksum_size);
   payload.insert(payload.end(), value.begin(), value.end());
   payload.insert(payload.end(), checksum.begin(), checksum.begin() + checksum_size);

   return std::string{prefix} +
          forge::codec::base64::encode(
              payload, {.characters = forge::codec::base64::alphabet::url, .pad = forge::codec::base64::padding::omit});
}

[[nodiscard]] bool valid_public_key(const public_key& value) noexcept {
   try {
      const auto point = bls12_381::g1::fromAffineBytesLE(value.serialize(), {.check_valid = true, .to_mont = true});
      return point && !point->isZero() && point->inCorrectSubgroup();
   } catch (...) {
      return false;
   }
}

template <typename Signature> [[nodiscard]] bool valid_signature(const Signature& value) noexcept {
   try {
      const auto point = bls12_381::g2::fromAffineBytesLE(value.serialize(), {.check_valid = true, .to_mont = true});
      return point && !point->isZero() && point->inCorrectSubgroup();
   } catch (...) {
      return false;
   }
}

} // namespace

namespace encoding {

public_key parse_public_key(std::string_view text) {
   const auto bytes = decode_checked<public_key::size_bytes>(text, public_key_prefix);
   const auto value = public_key{std::span<const std::uint8_t, public_key::size_bytes>{bytes}};
   if (!valid_public_key(value)) {
      FORGE_THROW_EXCEPTION(exceptions::parse_error, "BLS public key does not encode a valid point");
   }
   return value;
}

signature parse_signature(std::string_view text) {
   const auto bytes = decode_checked<signature::size_bytes>(text, signature_prefix);
   const auto value = signature{std::span<const std::uint8_t, signature::size_bytes>{bytes}};
   if (!valid_signature(value)) {
      FORGE_THROW_EXCEPTION(exceptions::parse_error, "BLS signature does not encode a valid point");
   }
   return value;
}

aggregate_signature parse_aggregate_signature(std::string_view text) {
   const auto bytes = decode_checked<aggregate_signature::size_bytes>(text, signature_prefix);
   const auto value = aggregate_signature{std::span<const std::uint8_t, aggregate_signature::size_bytes>{bytes}};
   if (!valid_signature(value)) {
      FORGE_THROW_EXCEPTION(exceptions::parse_error, "BLS aggregate signature does not encode a valid point");
   }
   return value;
}

std::string format(const public_key& value) {
   return encode_checked(value.bytes(), public_key_prefix);
}

std::string format(const signature& value) {
   return encode_checked(value.bytes(), signature_prefix);
}

std::string format(const aggregate_signature& value) {
   return encode_checked(value.bytes(), signature_prefix);
}

} // namespace encoding

void to_variant(const public_key& value, variant& output) {
   output = encoding::format(value);
}

void from_variant(const variant& value, public_key& output) {
   output = encoding::parse_public_key(value.as_string());
}

void to_variant(const signature& value, variant& output) {
   output = encoding::format(value);
}

void from_variant(const variant& value, signature& output) {
   output = encoding::parse_signature(value.as_string());
}

void to_variant(const aggregate_signature& value, variant& output) {
   output = encoding::format(value);
}

void from_variant(const variant& value, aggregate_signature& output) {
   output = encoding::parse_aggregate_signature(value.as_string());
}

} // namespace forge::crypto::bls
