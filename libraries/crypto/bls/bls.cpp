module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.crypto.bls;

import forge.codec.base64;
import forge.crypto.digest.ripemd160;
import forge.exceptions;
import forge.variant.value;

namespace forge::crypto::bls {

namespace {

constexpr auto private_key_prefix = std::string_view{"PVT_BLS_"};
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

[[nodiscard]] std::optional<bls12_381::g1> parse_public_point(const public_key& value) noexcept {
   try {
      auto point = bls12_381::g1::fromAffineBytesLE(value.serialize(), {.check_valid = true, .to_mont = true});
      if (!point || point->isZero()) {
         return std::nullopt;
      }
      return point;
   } catch (...) {
      return std::nullopt;
   }
}

template <typename Signature>
[[nodiscard]] std::optional<bls12_381::g2> parse_signature_point(const Signature& value) noexcept {
   try {
      auto point = bls12_381::g2::fromAffineBytesLE(value.serialize(), {.check_valid = true, .to_mont = true});
      if (!point || point->isZero()) {
         return std::nullopt;
      }
      return point;
   } catch (...) {
      return std::nullopt;
   }
}

[[nodiscard]] bool valid_private_secret(const std::array<std::uint64_t, 4>& secret) noexcept {
   if (std::all_of(secret.begin(), secret.end(), [](auto word) { return word == 0U; })) {
      return false;
   }
   const auto encoded = bls12_381::sk_to_bytes(secret);
   return bls12_381::sk_from_bytes(encoded) == secret;
}

} // namespace

struct proof_verified_public_key::impl {
   explicit impl(bls12_381::g1 value) : point(std::move(value)) {}

   bls12_381::g1 point;
};

proof_verified_public_key::proof_verified_public_key(public_key key, std::unique_ptr<impl> implementation)
    : key_(std::move(key)), impl_(std::move(implementation)) {}

proof_verified_public_key::proof_verified_public_key(const proof_verified_public_key& other)
    : key_(other.key_), impl_(other.impl_ ? std::make_unique<impl>(*other.impl_) : nullptr) {}

proof_verified_public_key::proof_verified_public_key(proof_verified_public_key&& other) noexcept = default;

proof_verified_public_key& proof_verified_public_key::operator=(const proof_verified_public_key& other) {
   if (this != &other) {
      auto implementation = other.impl_ ? std::make_unique<impl>(*other.impl_) : nullptr;
      key_ = other.key_;
      impl_ = std::move(implementation);
   }
   return *this;
}

proof_verified_public_key& proof_verified_public_key::operator=(proof_verified_public_key&& other) noexcept = default;

proof_verified_public_key::~proof_verified_public_key() = default;

struct signature_accumulator::impl {
   std::optional<bls12_381::g2> point;
};

signature_accumulator::signature_accumulator() : impl_(std::make_unique<impl>()) {}

signature_accumulator::signature_accumulator(const signature_accumulator& other)
    : impl_(other.impl_ ? std::make_unique<impl>(*other.impl_) : nullptr) {}

signature_accumulator::signature_accumulator(signature_accumulator&& other) noexcept = default;

signature_accumulator& signature_accumulator::operator=(const signature_accumulator& other) {
   if (this != &other) {
      impl_ = other.impl_ ? std::make_unique<impl>(*other.impl_) : nullptr;
   }
   return *this;
}

signature_accumulator& signature_accumulator::operator=(signature_accumulator&& other) noexcept = default;

signature_accumulator::~signature_accumulator() = default;

void signature_accumulator::add(const signature& value) {
   auto point = parse_signature_point(value);
   if (!point) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_signature, "cannot accumulate an invalid BLS signature");
   }
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_accumulator, "cannot use a moved-from BLS signature accumulator");
   }
   if (impl_->point) {
      impl_->point->addAssign(*point);
   } else {
      impl_->point = std::move(*point);
   }
}

void signature_accumulator::add(const aggregate_signature& value) {
   auto point = parse_signature_point(value);
   if (!point) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_signature, "cannot accumulate an invalid BLS aggregate signature");
   }
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_accumulator, "cannot use a moved-from BLS signature accumulator");
   }
   if (impl_->point) {
      impl_->point->addAssign(*point);
   } else {
      impl_->point = std::move(*point);
   }
}

aggregate_signature signature_accumulator::finish() const {
   if (!impl_ || !impl_->point) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_accumulator, "BLS signature accumulator is empty");
   }
   const auto bytes = impl_->point->toAffineBytesLE(bls12_381::from_mont::yes);
   return aggregate_signature{std::span<const std::uint8_t, aggregate_signature::size_bytes>{bytes}};
}

bool valid(const public_key& value) noexcept {
   return parse_public_point(value).has_value();
}

bool valid(const signature& value) noexcept {
   return parse_signature_point(value).has_value();
}

bool valid(const aggregate_signature& value) noexcept {
   return parse_signature_point(value).has_value();
}

bool verify(const public_key& key, std::span<const std::uint8_t> message, const signature& value) noexcept {
   const auto public_point = parse_public_point(key);
   const auto signature_point = parse_signature_point(value);
   if (!public_point || !signature_point) {
      return false;
   }
   try {
      return bls12_381::verify(*public_point, message, *signature_point);
   } catch (...) {
      return false;
   }
}

bool verify(const proof_verified_public_key& key, std::span<const std::uint8_t> message,
            const signature& value) noexcept {
   const auto signature_point = parse_signature_point(value);
   if (!key.impl_ || !signature_point) {
      return false;
   }
   try {
      return bls12_381::verify(key.impl_->point, message, *signature_point);
   } catch (...) {
      return false;
   }
}

std::optional<proof_verified_public_key> verify_proof_of_possession(const public_key& key,
                                                                    const signature& proof) noexcept {
   try {
      auto public_point = parse_public_point(key);
      const auto proof_point = parse_signature_point(proof);
      if (!public_point || !proof_point) {
         return std::nullopt;
      }
      if (!bls12_381::pop_verify(*public_point, *proof_point)) {
         return std::nullopt;
      }
      return proof_verified_public_key{key,
                                       std::make_unique<proof_verified_public_key::impl>(std::move(*public_point))};
   } catch (...) {
      return std::nullopt;
   }
}

bool verify_grouped(std::span<const aggregate_verification_group> groups, const aggregate_signature& value) {
   const auto signature_point = parse_signature_point(value);
   if (groups.empty() || !signature_point) {
      return false;
   }

   auto public_keys = std::vector<bls12_381::g1>{};
   auto messages = std::vector<std::vector<std::uint8_t>>{};
   public_keys.reserve(groups.size());
   messages.reserve(groups.size());

   for (const auto& group : groups) {
      if (group.public_keys.empty()) {
         return false;
      }

      auto group_keys = std::vector<bls12_381::g1>{};
      group_keys.reserve(group.public_keys.size());
      for (const auto& key : group.public_keys) {
         if (!key.impl_) {
            return false;
         }
         group_keys.push_back(key.impl_->point);
      }
      public_keys.push_back(bls12_381::aggregate_public_keys(group_keys));
      messages.emplace_back(group.message.begin(), group.message.end());
   }

   try {
      return bls12_381::aggregate_verify(public_keys, messages, *signature_point, true);
   } catch (...) {
      return false;
   }
}

namespace encoding {

private_key parse_private_key(std::string_view text) {
   const auto bytes = decode_checked<sizeof(private_key::_secret)>(text, private_key_prefix);
   auto value = private_key{};
   std::memcpy(value._secret.data(), bytes.data(), bytes.size());
   if (!valid_private_secret(value._secret)) {
      FORGE_THROW_EXCEPTION(exceptions::parse_error, "BLS private key is invalid");
   }
   return value;
}

std::string format(const private_key& value) {
   return encode_checked(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(value._secret.data()),
                                                       sizeof(value._secret)},
                         private_key_prefix);
}

} // namespace encoding

void to_variant(const private_key& value, variant& output) {
   output = encoding::format(value);
}

void from_variant(const variant& value, private_key& output) {
   output = encoding::parse_private_key(value.as_string());
}

} // namespace forge::crypto::bls
