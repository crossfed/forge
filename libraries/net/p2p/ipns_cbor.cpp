module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module forge.net.p2p.ipns;

import forge.exceptions;
import forge.net.p2p.exceptions;

#include "details/ipns_cbor.hxx"

namespace forge::net::p2p::detail::ipns_cbor {
namespace {

constexpr auto value_key = std::string_view{"Value"};
constexpr auto validity_key = std::string_view{"Validity"};
constexpr auto validity_type_key = std::string_view{"ValidityType"};
constexpr auto sequence_key = std::string_view{"Sequence"};
constexpr auto ttl_key = std::string_view{"TTL"};

[[noreturn]] void throw_cbor(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::codec_error, std::move(message));
}

[[noreturn]] void throw_cbor_options(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_options, std::move(message));
}

[[nodiscard]] bool valid_utf8(std::span<const std::uint8_t> value) noexcept;

[[nodiscard]] std::size_t argument_size(std::uint64_t value) noexcept {
   if (value < 24U) {
      return 1;
   }
   if (value <= (std::numeric_limits<std::uint8_t>::max)()) {
      return 2;
   }
   if (value <= (std::numeric_limits<std::uint16_t>::max)()) {
      return 3;
   }
   if (value <= (std::numeric_limits<std::uint32_t>::max)()) {
      return 5;
   }
   return 9;
}

void require_growth(const std::vector<std::uint8_t>& out, std::size_t size) {
   if (out.size() > ipns::max_record_size || size > ipns::max_record_size - out.size()) {
      throw_cbor_options("IPNS DAG-CBOR data exceeds record size limit");
   }
}

void require_field_growth(const std::vector<std::uint8_t>& out, std::size_t header, std::size_t payload) {
   require_growth(out, header);
   if (payload > ipns::max_record_size - out.size() - header) {
      throw_cbor_options("IPNS DAG-CBOR data exceeds record size limit");
   }
}

[[nodiscard]] bool reserved(std::string_view key) noexcept {
   return key == value_key || key == validity_key || key == validity_type_key || key == sequence_key || key == ttl_key;
}

void append_argument(std::vector<std::uint8_t>& out, std::uint8_t major, std::uint64_t value) {
   require_growth(out, argument_size(value));
   const auto prefix = static_cast<std::uint8_t>(major << 5U);
   if (value < 24U) {
      out.push_back(static_cast<std::uint8_t>(prefix | value));
      return;
   }
   if (value <= (std::numeric_limits<std::uint8_t>::max)()) {
      out.push_back(static_cast<std::uint8_t>(prefix | 24U));
      out.push_back(static_cast<std::uint8_t>(value));
      return;
   }
   if (value <= (std::numeric_limits<std::uint16_t>::max)()) {
      out.push_back(static_cast<std::uint8_t>(prefix | 25U));
      out.push_back(static_cast<std::uint8_t>(value >> 8U));
      out.push_back(static_cast<std::uint8_t>(value));
      return;
   }
   if (value <= (std::numeric_limits<std::uint32_t>::max)()) {
      out.push_back(static_cast<std::uint8_t>(prefix | 26U));
      for (auto shift = 24; shift >= 0; shift -= 8) {
         out.push_back(static_cast<std::uint8_t>(value >> shift));
      }
      return;
   }
   out.push_back(static_cast<std::uint8_t>(prefix | 27U));
   for (auto shift = 56; shift >= 0; shift -= 8) {
      out.push_back(static_cast<std::uint8_t>(value >> shift));
   }
}

void append_integer(std::vector<std::uint8_t>& out, std::int64_t value) {
   if (value >= 0) {
      append_argument(out, 0, static_cast<std::uint64_t>(value));
      return;
   }
   append_argument(out, 1, static_cast<std::uint64_t>(-(value + 1)));
}

void append_bytes(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> value) {
   const auto header = argument_size(value.size());
   require_field_growth(out, header, value.size());
   append_argument(out, 2, value.size());
   out.insert(out.end(), value.begin(), value.end());
}

void append_text(std::vector<std::uint8_t>& out, std::string_view value) {
   const auto bytes = std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
   if (!valid_utf8(bytes)) {
      throw_cbor_options("IPNS DAG-CBOR text is not valid UTF-8");
   }
   const auto header = argument_size(value.size());
   require_field_growth(out, header, value.size());
   append_argument(out, 3, value.size());
   out.insert(out.end(), value.begin(), value.end());
}

void append_metadata(std::vector<std::uint8_t>& out, const ipns::metadata_value& value) {
   std::visit(
       [&out](const auto& item) {
          using item_type = std::decay_t<decltype(item)>;
          if constexpr (std::is_same_v<item_type, std::string>) {
             append_text(out, item);
          } else if constexpr (std::is_same_v<item_type, std::vector<std::uint8_t>>) {
             append_bytes(out, item);
          } else if constexpr (std::is_same_v<item_type, std::int64_t>) {
             append_integer(out, item);
          } else {
             require_growth(out, 1);
             out.push_back(item ? 0xf5U : 0xf4U);
          }
       },
       value);
}

[[nodiscard]] bool valid_utf8(std::span<const std::uint8_t> value) noexcept {
   auto offset = std::size_t{};
   while (offset < value.size()) {
      const auto first = value[offset++];
      if (first <= 0x7fU) {
         continue;
      }
      auto continuation = std::size_t{};
      auto minimum = std::uint32_t{};
      auto codepoint = std::uint32_t{};
      if ((first & 0xe0U) == 0xc0U) {
         continuation = 1;
         minimum = 0x80U;
         codepoint = first & 0x1fU;
      } else if ((first & 0xf0U) == 0xe0U) {
         continuation = 2;
         minimum = 0x800U;
         codepoint = first & 0x0fU;
      } else if ((first & 0xf8U) == 0xf0U) {
         continuation = 3;
         minimum = 0x10000U;
         codepoint = first & 0x07U;
      } else {
         return false;
      }
      if (continuation > value.size() - offset) {
         return false;
      }
      for (auto index = std::size_t{}; index < continuation; ++index) {
         const auto next = value[offset++];
         if ((next & 0xc0U) != 0x80U) {
            return false;
         }
         codepoint = (codepoint << 6U) | (next & 0x3fU);
      }
      if (codepoint < minimum || codepoint > 0x10ffffU || (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
         return false;
      }
   }
   return true;
}

void require_available(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t size) {
   if (size > bytes.size() - offset) {
      throw_cbor("truncated IPNS DAG-CBOR data");
   }
}

[[nodiscard]] std::uint64_t read_argument(std::span<const std::uint8_t> bytes, std::size_t& offset,
                                          std::uint8_t additional) {
   if (additional < 24U) {
      return additional;
   }
   auto read = [&](std::size_t size) {
      require_available(bytes, offset, size);
      auto value = std::uint64_t{};
      for (auto index = std::size_t{}; index < size; ++index) {
         value = (value << 8U) | bytes[offset++];
      }
      return value;
   };
   if (additional == 24U) {
      const auto value = read(1);
      if (value < 24U) {
         throw_cbor("non-canonical IPNS DAG-CBOR integer");
      }
      return value;
   }
   if (additional == 25U) {
      const auto value = read(2);
      if (value <= (std::numeric_limits<std::uint8_t>::max)()) {
         throw_cbor("non-canonical IPNS DAG-CBOR integer");
      }
      return value;
   }
   if (additional == 26U) {
      const auto value = read(4);
      if (value <= (std::numeric_limits<std::uint16_t>::max)()) {
         throw_cbor("non-canonical IPNS DAG-CBOR integer");
      }
      return value;
   }
   if (additional == 27U) {
      const auto value = read(8);
      if (value <= (std::numeric_limits<std::uint32_t>::max)()) {
         throw_cbor("non-canonical IPNS DAG-CBOR integer");
      }
      return value;
   }
   throw_cbor("indefinite or reserved IPNS DAG-CBOR value is unsupported");
}

[[nodiscard]] std::pair<std::uint8_t, std::uint64_t> read_head(std::span<const std::uint8_t> bytes,
                                                               std::size_t& offset) {
   require_available(bytes, offset, 1);
   const auto first = bytes[offset++];
   return {static_cast<std::uint8_t>(first >> 5U), read_argument(bytes, offset, first & 0x1fU)};
}

[[nodiscard]] std::vector<std::uint8_t> read_bytes(std::span<const std::uint8_t> bytes, std::size_t& offset) {
   const auto [major, size] = read_head(bytes, offset);
   if (major != 2U || size > bytes.size() - offset) {
      throw_cbor("IPNS DAG-CBOR field must be bounded bytes");
   }
   auto out = std::vector<std::uint8_t>{bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size)};
   offset += static_cast<std::size_t>(size);
   return out;
}

[[nodiscard]] std::string read_text(std::span<const std::uint8_t> bytes, std::size_t& offset) {
   const auto [major, size] = read_head(bytes, offset);
   if (major != 3U || size > bytes.size() - offset) {
      throw_cbor("IPNS DAG-CBOR map key must be bounded text");
   }
   const auto value = bytes.subspan(offset, static_cast<std::size_t>(size));
   if (!valid_utf8(value)) {
      throw_cbor("IPNS DAG-CBOR text is not valid UTF-8");
   }
   offset += static_cast<std::size_t>(size);
   return {value.begin(), value.end()};
}

[[nodiscard]] std::int64_t read_integer(std::span<const std::uint8_t> bytes, std::size_t& offset) {
   const auto [major, value] = read_head(bytes, offset);
   if (major == 0U) {
      if (value > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
         throw_cbor("IPNS DAG-CBOR integer exceeds int64");
      }
      return static_cast<std::int64_t>(value);
   }
   if (major == 1U) {
      if (value > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
         throw_cbor("IPNS DAG-CBOR integer is below int64");
      }
      return -1 - static_cast<std::int64_t>(value);
   }
   throw_cbor("IPNS DAG-CBOR field must be an integer");
}

[[nodiscard]] ipns::metadata_value read_metadata(std::span<const std::uint8_t> bytes, std::size_t& offset) {
   require_available(bytes, offset, 1);
   const auto major = static_cast<std::uint8_t>(bytes[offset] >> 5U);
   if (major == 0U || major == 1U) {
      return read_integer(bytes, offset);
   }
   if (major == 2U) {
      return read_bytes(bytes, offset);
   }
   if (major == 3U) {
      return read_text(bytes, offset);
   }
   if (bytes[offset] == 0xf4U || bytes[offset] == 0xf5U) {
      return bytes[offset++] == 0xf5U;
   }
   throw_cbor("IPNS DAG-CBOR metadata must be a scalar string, bytes, integer, or boolean");
}

} // namespace

std::vector<std::uint8_t> encode(const document& value) {
   if (value.metadata_values.size() > ipns::max_record_size / 2U) {
      throw_cbor_options("IPNS metadata contains too many entries");
   }
   auto keys = std::vector<std::string_view>{};
   keys.reserve(5 + value.metadata_values.size());
   for (const auto& [key, item] : value.metadata_values) {
      static_cast<void>(item);
      if (key.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "IPNS metadata key must not be empty");
      }
      if (reserved(key)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "IPNS metadata key uses a reserved field name");
      }
      if (!valid_utf8(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(key.data()), key.size()})) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "IPNS metadata key is not valid UTF-8");
      }
      keys.emplace_back(key);
   }
   keys.emplace_back(value_key);
   keys.emplace_back(validity_key);
   keys.emplace_back(validity_type_key);
   keys.emplace_back(sequence_key);
   keys.emplace_back(ttl_key);
   std::ranges::sort(keys, [](const auto& left, const auto& right) {
      return left.size() < right.size() || (left.size() == right.size() && left < right);
   });

   auto out = std::vector<std::uint8_t>{};
   append_argument(out, 5, keys.size());
   for (const auto& key : keys) {
      append_text(out, key);
      if (key == value_key) {
         append_bytes(out, value.value);
      } else if (key == validity_key) {
         append_bytes(out, value.validity);
      } else if (key == validity_type_key) {
         append_integer(out, value.validity_type);
      } else if (key == sequence_key) {
         append_integer(out, value.sequence);
      } else if (key == ttl_key) {
         append_integer(out, value.ttl);
      } else {
         const auto item = value.metadata_values.find(key);
         if (item == value.metadata_values.end()) {
            throw_cbor_options("IPNS metadata key disappeared during canonical encoding");
         }
         append_metadata(out, item->second);
      }
   }
   if (out.size() > ipns::max_record_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "IPNS DAG-CBOR data exceeds record size limit");
   }
   return out;
}

document decode(std::span<const std::uint8_t> bytes) {
   if (bytes.empty() || bytes.size() > ipns::max_record_size) {
      throw_cbor("IPNS DAG-CBOR data is empty or oversized");
   }
   auto offset = std::size_t{};
   const auto [major, count] = read_head(bytes, offset);
   if (major != 5U || count > bytes.size() / 2U) {
      throw_cbor("IPNS DAG-CBOR data must be a bounded map");
   }

   auto out = document{};
   auto seen = std::set<std::string, std::less<>>{};
   auto previous = std::string{};
   auto saw_value = false;
   auto saw_validity = false;
   auto saw_validity_type = false;
   auto saw_sequence = false;
   auto saw_ttl = false;
   for (auto index = std::uint64_t{}; index < count; ++index) {
      auto key = read_text(bytes, offset);
      if (!previous.empty() && (key.size() < previous.size() || (key.size() == previous.size() && key <= previous))) {
         throw_cbor("IPNS DAG-CBOR map keys are not in canonical order");
      }
      previous = key;
      if (!seen.insert(key).second) {
         throw_cbor("IPNS DAG-CBOR map contains a duplicate key");
      }

      if (key == value_key) {
         out.value = read_bytes(bytes, offset);
         saw_value = true;
      } else if (key == validity_key) {
         out.validity = read_bytes(bytes, offset);
         saw_validity = true;
      } else if (key == validity_type_key) {
         out.validity_type = read_integer(bytes, offset);
         saw_validity_type = true;
      } else if (key == sequence_key) {
         out.sequence = read_integer(bytes, offset);
         saw_sequence = true;
      } else if (key == ttl_key) {
         out.ttl = read_integer(bytes, offset);
         saw_ttl = true;
      } else {
         if (key.empty()) {
            throw_cbor("IPNS DAG-CBOR metadata key must not be empty");
         }
         out.metadata_values.emplace(std::move(key), read_metadata(bytes, offset));
      }
   }
   if (offset != bytes.size()) {
      throw_cbor("IPNS DAG-CBOR data has trailing bytes");
   }
   if (!saw_value || !saw_validity || !saw_validity_type || !saw_sequence || !saw_ttl) {
      throw_cbor("IPNS DAG-CBOR data is missing a required field");
   }
   if (out.validity_type != static_cast<std::int64_t>(ipns::validity_type::eol)) {
      throw_cbor("IPNS DAG-CBOR validity type is unsupported");
   }
   if (out.ttl < 0) {
      throw_cbor("IPNS DAG-CBOR TTL must not be negative");
   }
   return out;
}

} // namespace forge::net::p2p::detail::ipns_cbor
