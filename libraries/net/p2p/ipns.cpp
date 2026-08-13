module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.net.p2p.ipns;

import forge.crypto.asymmetric;
import forge.exceptions;
import forge.multiformats.multicodec;
import forge.multiformats.multihash;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;

#include "details/identity_signature.hxx"
#include "details/ipns_cbor.hxx"
#include "details/ipns_protobuf.hxx"

namespace forge::net::p2p::ipns {
namespace {

constexpr auto signature_v2_prefix = std::string_view{"ipns-signature:"};
[[noreturn]] void throw_invalid_options(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_options, std::move(message));
}

[[noreturn]] void throw_invalid_record(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::protocol_error, std::move(message));
}

[[noreturn]] void throw_invalid_key(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_identity, std::move(message));
}

void append_fixed_decimal(std::string& out, unsigned value, unsigned width) {
   auto buffer = std::array<char, 10>{};
   auto* begin = buffer.data();
   auto* end = begin + static_cast<std::ptrdiff_t>(buffer.size());
   const auto result = std::to_chars(begin, end, value);
   if (result.ec != std::errc{} || static_cast<unsigned>(result.ptr - begin) > width) {
      throw_invalid_options("IPNS EOL is outside RFC3339Nano range");
   }
   out.append(width - static_cast<unsigned>(result.ptr - begin), '0');
   out.append(begin, result.ptr);
}

[[nodiscard]] std::string format_rfc3339_nano(time_point value) {
   const auto day = std::chrono::floor<std::chrono::days>(value.whole_seconds());
   const auto date = std::chrono::year_month_day{day};
   const auto year = static_cast<int>(date.year());
   if (!date.ok() || year < 0 || year > 9999) {
      throw_invalid_options("IPNS EOL is outside RFC3339Nano range");
   }
   const auto time = std::chrono::hh_mm_ss<std::chrono::nanoseconds>{(value.whole_seconds() - day) + value.subsecond()};
   auto out = std::string{};
   out.reserve(30);
   append_fixed_decimal(out, static_cast<unsigned>(year), 4);
   out.push_back('-');
   append_fixed_decimal(out, static_cast<unsigned>(date.month()), 2);
   out.push_back('-');
   append_fixed_decimal(out, static_cast<unsigned>(date.day()), 2);
   out.push_back('T');
   append_fixed_decimal(out, static_cast<unsigned>(time.hours().count()), 2);
   out.push_back(':');
   append_fixed_decimal(out, static_cast<unsigned>(time.minutes().count()), 2);
   out.push_back(':');
   append_fixed_decimal(out, static_cast<unsigned>(time.seconds().count()), 2);
   auto fraction = time.subseconds().count();
   if (fraction != 0) {
      auto digits = std::string{};
      digits.reserve(9);
      append_fixed_decimal(digits, static_cast<unsigned>(fraction), 9);
      while (digits.back() == '0') {
         digits.pop_back();
      }
      out.push_back('.');
      out += digits;
   }
   out.push_back('Z');
   return out;
}

[[nodiscard]] unsigned decimal(std::string_view value, std::size_t offset, std::size_t count) {
   if (count == 0 || offset > value.size() || count > value.size() - offset) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "truncated IPNS RFC3339Nano EOL");
   }
   auto out = unsigned{};
   const auto result = std::from_chars(value.data() + static_cast<std::ptrdiff_t>(offset),
                                       value.data() + static_cast<std::ptrdiff_t>(offset + count), out);
   if (result.ec != std::errc{} || result.ptr != value.data() + static_cast<std::ptrdiff_t>(offset + count)) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "invalid IPNS RFC3339Nano EOL digits");
   }
   return out;
}

[[nodiscard]] time_point parse_rfc3339_nano(std::span<const std::uint8_t> bytes) {
   const auto value = std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
   if (value.size() < 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
       value[16] != ':') {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "invalid IPNS RFC3339Nano EOL shape");
   }
   const auto year_value = decimal(value, 0, 4);
   const auto month_value = decimal(value, 5, 2);
   const auto day_value = decimal(value, 8, 2);
   const auto hour = decimal(value, 11, 2);
   const auto minute = decimal(value, 14, 2);
   const auto second = decimal(value, 17, 2);
   const auto date = std::chrono::year_month_day{std::chrono::year{static_cast<int>(year_value)},
                                                 std::chrono::month{month_value}, std::chrono::day{day_value}};
   if (!date.ok() || hour > 23 || minute > 59 || second > 59) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "invalid IPNS RFC3339Nano EOL date or time");
   }

   auto offset = std::size_t{19};
   auto fraction = std::int64_t{};
   if (offset < value.size() && value[offset] == '.') {
      const auto begin = ++offset;
      while (offset < value.size() && value[offset] >= '0' && value[offset] <= '9') {
         ++offset;
      }
      const auto digits = offset - begin;
      if (digits == 0) {
         FORGE_THROW_EXCEPTION(exceptions::codec_error, "invalid IPNS RFC3339Nano fractional seconds");
      }
      const auto retained = std::min<std::size_t>(digits, 9);
      fraction = decimal(value, begin, retained);
      for (auto remaining = retained; remaining < 9; ++remaining) {
         fraction *= 10;
      }
   }

   auto zone_offset = std::int64_t{};
   if (offset < value.size() && value[offset] == 'Z') {
      ++offset;
   } else if (offset < value.size() && (value[offset] == '+' || value[offset] == '-')) {
      const auto negative = value[offset++] == '-';
      if (offset + 5 != value.size() || value[offset + 2] != ':') {
         FORGE_THROW_EXCEPTION(exceptions::codec_error, "invalid IPNS RFC3339Nano timezone");
      }
      const auto zone_hour = decimal(value, offset, 2);
      const auto zone_minute = decimal(value, offset + 3, 2);
      if (zone_hour > 23 || zone_minute > 59) {
         FORGE_THROW_EXCEPTION(exceptions::codec_error, "invalid IPNS RFC3339Nano timezone offset");
      }
      zone_offset = static_cast<std::int64_t>(zone_hour * 60U + zone_minute) * 60;
      if (negative) {
         zone_offset = -zone_offset;
      }
      offset += 5;
   } else {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "IPNS RFC3339Nano EOL is missing timezone");
   }
   if (offset != value.size()) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "IPNS RFC3339Nano EOL has trailing data");
   }

   const auto local = std::chrono::sys_seconds{std::chrono::sys_days{date}} + std::chrono::hours{hour} +
                      std::chrono::minutes{minute} + std::chrono::seconds{second};
   const auto seconds = local.time_since_epoch().count() - zone_offset;
   return time_point{std::chrono::sys_seconds{std::chrono::seconds{seconds}}, std::chrono::nanoseconds{fraction}};
}

[[nodiscard]] std::vector<std::uint8_t> signature_v2_data(std::span<const std::uint8_t> data) {
   auto out = std::vector<std::uint8_t>{signature_v2_prefix.begin(), signature_v2_prefix.end()};
   out.insert(out.end(), data.begin(), data.end());
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> signature_v1_data(std::span<const std::uint8_t> value,
                                                          std::span<const std::uint8_t> validity) {
   auto out = std::vector<std::uint8_t>{value.begin(), value.end()};
   out.insert(out.end(), validity.begin(), validity.end());
   out.push_back('0');
   return out;
}

[[nodiscard]] std::optional<public_key> extract_inline_key(const peer_id& peer) {
   try {
      const auto hash = forge::multiformats::multihash::decode(peer.to_bytes());
      if (hash.code != forge::multiformats::code_value(forge::multiformats::multicodec_code::identity)) {
         return std::nullopt;
      }
      return decode_public_key(hash.digest);
   } catch (const forge::exceptions::base& error) {
      if (exceptions::is(error, exceptions::code::invalid_identity)) {
         throw;
      }
      throw_invalid_key(error.what());
   }
}

[[nodiscard]] public_key validation_key(const std::optional<std::vector<std::uint8_t>>& embedded_key,
                                        const peer_id& expected_peer, const std::optional<public_key>& external_key,
                                        const public_key_resolver* resolver) {
   if (!valid_peer_id(expected_peer)) {
      throw_invalid_key("IPNS validation Peer ID is invalid");
   }

   auto selected = std::optional<public_key>{};
   if (embedded_key && !embedded_key->empty()) {
      try {
         selected = decode_public_key(*embedded_key);
      } catch (const forge::exceptions::base& error) {
         throw_invalid_key(error.what());
      }
   } else {
      selected = extract_inline_key(expected_peer);
      if (!selected && external_key) {
         selected = *external_key;
      }
      if (!selected && resolver) {
         selected = (*resolver)(expected_peer);
      }
      if (!selected) {
         throw_invalid_key("IPNS public key is not embedded, inline, or available from the KeyBook");
      }
   }

   try {
      if (make_peer_id(*selected) != expected_peer) {
         throw_invalid_key("IPNS public key does not match the expected Peer ID");
      }
      if (external_key && !embedded_key && make_peer_id(*external_key) != expected_peer) {
         throw_invalid_key("external IPNS public key does not match the expected Peer ID");
      }
   } catch (const forge::exceptions::base& error) {
      if (exceptions::is(error, exceptions::code::invalid_identity)) {
         throw;
      }
      throw_invalid_key(error.what());
   }
   return *selected;
}

[[nodiscard]] std::span<const std::uint8_t>
optional_bytes(const std::optional<std::vector<std::uint8_t>>& value) noexcept {
   if (!value) {
      return {};
   }
   return *value;
}

} // namespace

time_point::time_point(std::chrono::sys_seconds whole_seconds, std::chrono::nanoseconds subsecond)
    : whole_seconds_{whole_seconds}, subsecond_{subsecond} {
   if (subsecond_ < std::chrono::nanoseconds::zero() || subsecond_ >= std::chrono::seconds{1}) {
      throw_invalid_options("IPNS time subsecond must be in [0, 1 second)");
   }
}

time_point::time_point(std::chrono::sys_time<std::chrono::nanoseconds> value) {
   whole_seconds_ = std::chrono::floor<std::chrono::seconds>(value);
   subsecond_ = value - whole_seconds_;
}

time_point time_point::now() {
   return time_point{std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now())};
}

std::chrono::sys_seconds time_point::whole_seconds() const noexcept {
   return whole_seconds_;
}

std::chrono::nanoseconds time_point::subsecond() const noexcept {
   return subsecond_;
}

std::strong_ordering operator<=>(const time_point& left, const time_point& right) noexcept {
   if (const auto seconds = left.whole_seconds_ <=> right.whole_seconds_; seconds != 0) {
      return seconds;
   }
   return left.subsecond_ <=> right.subsecond_;
}

std::span<const std::uint8_t> record::value() const noexcept {
   return value_;
}

std::uint64_t record::sequence() const noexcept {
   return sequence_;
}

time_point record::eol() const noexcept {
   return eol_;
}

std::string_view record::eol_text() const noexcept {
   return eol_text_;
}

std::chrono::nanoseconds record::ttl() const noexcept {
   return ttl_;
}

validity_type record::validity() const noexcept {
   return validity_;
}

const metadata& record::metadata_values() const noexcept {
   return metadata_;
}

std::optional<public_key> record::embedded_public_key() const {
   if (!public_key_ || public_key_->empty()) {
      return std::nullopt;
   }
   return decode_public_key(*public_key_);
}

bool record::has_v1_compatibility() const noexcept {
   return signature_v1_.has_value() || value_v1_.has_value();
}

bool record::has_v2_signature() const noexcept {
   return signature_v2_.has_value();
}

std::span<const std::uint8_t> record::signature_v1() const noexcept {
   return optional_bytes(signature_v1_);
}

std::span<const std::uint8_t> record::signature_v2() const noexcept {
   return optional_bytes(signature_v2_);
}

std::span<const std::uint8_t> record::data() const noexcept {
   return optional_bytes(data_);
}

std::span<const std::uint8_t> record::encoded() const noexcept {
   return encoded_;
}

record create(const public_key& key, const signing_callback& signer, std::span<const std::uint8_t> value,
              std::uint64_t sequence, time_point eol, std::chrono::nanoseconds ttl, create_options options) {
   if (!signer) {
      throw_invalid_options("IPNS signing callback is empty");
   }
   if (ttl.count() < 0) {
      throw_invalid_options("IPNS TTL must not be negative");
   }
   if (value.size() > max_record_size) {
      throw_invalid_options("IPNS value exceeds the record size limit");
   }
   try {
      static_cast<void>(crypto_public_key(key));
      static_cast<void>(make_peer_id(key));
   } catch (const forge::exceptions::base& error) {
      throw_invalid_options(error.what());
   }

   const auto validity = format_rfc3339_nano(eol);
   auto cbor = detail::ipns_cbor::encode(detail::ipns_cbor::document{
       .value = {value.begin(), value.end()},
       .validity = {validity.begin(), validity.end()},
       .validity_type = static_cast<std::int64_t>(validity_type::eol),
       .sequence = std::bit_cast<std::int64_t>(sequence),
       .ttl = ttl.count(),
       .metadata_values = std::move(options.metadata_values),
   });
   auto signature_v2 = signer(signature_v2_data(cbor));
   if (signature_v2.empty()) {
      throw_invalid_options("IPNS signing callback returned an empty V2 signature");
   }
   if (signature_v2.size() > max_record_size) {
      throw_invalid_options("IPNS signing callback returned an oversized V2 signature");
   }
   if (!verify_identity_signature(key, signature_v2_data(cbor), signature_v2)) {
      throw_invalid_options("IPNS signing callback returned a V2 signature for a different key");
   }

   auto wire = detail::ipns_protobuf::wire_record{
       .signature_v2 = std::move(signature_v2),
       .data = std::move(cbor),
   };
   if (options.v1_compatibility) {
      wire.value = std::vector<std::uint8_t>{value.begin(), value.end()};
      wire.validity_type = static_cast<std::uint64_t>(validity_type::eol);
      wire.validity = std::vector<std::uint8_t>{validity.begin(), validity.end()};
      wire.sequence = sequence;
      wire.ttl = static_cast<std::uint64_t>(ttl.count());
      wire.signature_v1 = signer(signature_v1_data(*wire.value, *wire.validity));
      if (wire.signature_v1->empty()) {
         throw_invalid_options("IPNS signing callback returned an empty V1 signature");
      }
      if (wire.signature_v1->size() > max_record_size) {
         throw_invalid_options("IPNS signing callback returned an oversized V1 signature");
      }
      if (!verify_identity_signature(key, signature_v1_data(*wire.value, *wire.validity), *wire.signature_v1)) {
         throw_invalid_options("IPNS signing callback returned a V1 signature for a different key");
      }
   }

   auto embed = options.embed_public_key.value_or(false);
   if (!options.embed_public_key) {
      const auto hash = forge::multiformats::multihash::decode(make_peer_id(key).to_bytes());
      embed = hash.code != forge::multiformats::code_value(forge::multiformats::multicodec_code::identity);
   }
   if (embed) {
      wire.public_key = encode_public_key(key);
   }

   auto encoded = detail::ipns_protobuf::encode(wire);
   if (encoded.size() > max_record_size) {
      throw_invalid_options("IPNS record exceeds 10 KiB");
   }
   return decode(encoded);
}

record decode(std::span<const std::uint8_t> bytes) {
   if (bytes.size() > max_record_size) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "IPNS record exceeds 10 KiB");
   }
   auto wire = detail::ipns_protobuf::decode(bytes);
   if (!wire.data || wire.data->empty()) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "IPNS record is missing DAG-CBOR Data");
   }
   auto document = detail::ipns_cbor::decode(*wire.data);
   auto out = record{};
   out.encoded_ = {bytes.begin(), bytes.end()};
   out.value_v1_ = std::move(wire.value);
   out.signature_v1_ = std::move(wire.signature_v1);
   out.validity_type_v1_ = wire.validity_type;
   out.validity_v1_ = std::move(wire.validity);
   out.sequence_v1_ = wire.sequence;
   out.ttl_v1_ = wire.ttl;
   out.public_key_ = std::move(wire.public_key);
   out.signature_v2_ = std::move(wire.signature_v2);
   out.data_ = std::move(wire.data);
   out.unknown_protobuf_fields_ = std::move(wire.unknown_fields);
   out.value_ = std::move(document.value);
   out.sequence_ = std::bit_cast<std::uint64_t>(document.sequence);
   out.eol_text_ = {document.validity.begin(), document.validity.end()};
   out.eol_ = parse_rfc3339_nano(document.validity);
   out.ttl_ = std::chrono::nanoseconds{document.ttl};
   out.validity_ = static_cast<validity_type>(document.validity_type);
   out.metadata_ = std::move(document.metadata_values);
   return out;
}

std::vector<std::uint8_t> encode(const record& value) {
   return value.encoded_;
}

void validate(const record& value, const peer_id& expected_peer, std::optional<public_key> external_key,
              time_point now) {
   const auto resolver = [external_key = std::move(external_key)](const peer_id&) { return external_key; };
   validate(value, expected_peer, public_key_resolver{resolver}, now);
}

void validate(const record& value, const peer_id& expected_peer, const public_key_resolver& resolver, time_point now) {
   if (!resolver) {
      throw_invalid_options("IPNS public key resolver is empty");
   }
   if (value.encoded_.size() > max_record_size) {
      throw_invalid_record("IPNS record exceeds 10 KiB");
   }
   if (!value.signature_v2_ || value.signature_v2_->empty() || !value.data_ || value.data_->empty()) {
      throw_invalid_record("IPNS record requires SignatureV2 and Data");
   }
   const auto key = validation_key(value.public_key_, expected_peer, std::nullopt, &resolver);
   if (!verify_identity_signature(key, signature_v2_data(*value.data_), *value.signature_v2_)) {
      throw_invalid_key("IPNS SignatureV2 verification failed");
   }

   const auto has_legacy_data =
       (value.signature_v1_ && !value.signature_v1_->empty()) || (value.value_v1_ && !value.value_v1_->empty());
   if (has_legacy_data) {
      const auto empty = std::vector<std::uint8_t>{};
      const auto& legacy_value = value.value_v1_ ? *value.value_v1_ : empty;
      const auto& legacy_validity = value.validity_v1_ ? *value.validity_v1_ : empty;
      if (legacy_value != value.value_ ||
          legacy_validity != std::vector<std::uint8_t>{value.eol_text_.begin(), value.eol_text_.end()} ||
          value.validity_type_v1_.value_or(0) != static_cast<std::uint64_t>(value.validity_) ||
          value.sequence_v1_.value_or(0) != value.sequence_ ||
          value.ttl_v1_.value_or(0) != static_cast<std::uint64_t>(value.ttl_.count())) {
         throw_invalid_record("IPNS V1 protobuf fields do not match signed DAG-CBOR Data");
      }
   }
   if (now > value.eol_) {
      throw_invalid_record("IPNS record is expired");
   }
}

std::size_t select(std::span<const record> candidates) {
   if (candidates.empty()) {
      throw_invalid_options("IPNS selector requires at least one record");
   }
   auto best = std::size_t{};
   for (auto index = std::size_t{1}; index < candidates.size(); ++index) {
      const auto& current = candidates[best];
      const auto& candidate = candidates[index];
      auto newer = false;
      if (current.signature_v2_.has_value() != candidate.signature_v2_.has_value()) {
         newer = candidate.signature_v2_.has_value();
      } else if (current.sequence_ != candidate.sequence_) {
         newer = candidate.sequence_ > current.sequence_;
      } else if (current.eol_ != candidate.eol_) {
         newer = candidate.eol_ > current.eol_;
      } else {
         newer = std::lexicographical_compare(current.encoded_.begin(), current.encoded_.end(),
                                              candidate.encoded_.begin(), candidate.encoded_.end());
      }
      if (newer) {
         best = index;
      }
   }
   return best;
}

std::vector<std::uint8_t> routing_key(const peer_id& peer) {
   if (!valid_peer_id(peer)) {
      throw_invalid_key("cannot derive an IPNS routing key from an invalid Peer ID");
   }
   auto out = std::vector<std::uint8_t>{routing_prefix.begin(), routing_prefix.end()};
   const auto bytes = peer.to_bytes();
   out.insert(out.end(), bytes.begin(), bytes.end());
   return out;
}

} // namespace forge::net::p2p::ipns
