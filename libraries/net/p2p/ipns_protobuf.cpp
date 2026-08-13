module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

module forge.net.p2p.ipns;

import forge.exceptions;
import forge.multiformats.exceptions;
import forge.multiformats.varint;
import forge.net.p2p.exceptions;

#include "details/ipns_protobuf.hxx"
#include "details/protobuf.hxx"

namespace forge::net::p2p::detail::ipns_protobuf {
namespace {

[[noreturn]] void throw_protobuf(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::codec_error, std::move(message));
}

[[noreturn]] void throw_protobuf_options(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_options, std::move(message));
}

[[nodiscard]] std::size_t varint_size(std::uint64_t value) noexcept {
   auto size = std::size_t{1};
   while (value >= 0x80U) {
      value >>= 7U;
      ++size;
   }
   return size;
}

void require_growth(const std::vector<std::uint8_t>& out, std::size_t size) {
   if (out.size() > ipns::max_record_size || size > ipns::max_record_size - out.size()) {
      throw_protobuf_options("IPNS protobuf record exceeds 10 KiB");
   }
}

void require_field_growth(const std::vector<std::uint8_t>& out, std::size_t key, std::size_t length,
                          std::size_t payload = 0) {
   auto remaining = ipns::max_record_size - out.size();
   for (const auto part : {key, length, payload}) {
      if (part > remaining) {
         throw_protobuf_options("IPNS protobuf record exceeds 10 KiB");
      }
      remaining -= part;
   }
}

void require_available(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t size) {
   if (size > bytes.size() - offset) {
      throw_protobuf("truncated IPNS protobuf field");
   }
}

[[nodiscard]] std::uint64_t read_varint(std::span<const std::uint8_t> bytes, std::size_t& offset) {
   try {
      const auto value = forge::multiformats::varint_decode(bytes.subspan(offset));
      offset += value.size;
      return value.value;
   } catch (const forge::multiformats::exceptions::invalid_format& error) {
      throw_protobuf(error.what());
   }
}

[[nodiscard]] std::vector<std::uint8_t> read_bytes(std::span<const std::uint8_t> bytes, std::size_t& offset) {
   const auto size = read_varint(bytes, offset);
   if (size > bytes.size() - offset) {
      throw_protobuf("truncated IPNS protobuf bytes field");
   }
   auto out = std::vector<std::uint8_t>{bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size)};
   offset += static_cast<std::size_t>(size);
   return out;
}

void skip_unknown(std::span<const std::uint8_t> bytes, std::size_t& offset, std::uint8_t wire) {
   switch (wire) {
   case 0:
      static_cast<void>(read_varint(bytes, offset));
      return;
   case 1:
      require_available(bytes, offset, 8);
      offset += 8;
      return;
   case 2: {
      const auto size = read_varint(bytes, offset);
      if (size > bytes.size() - offset) {
         throw_protobuf("truncated unknown IPNS protobuf bytes field");
      }
      offset += static_cast<std::size_t>(size);
      return;
   }
   case 5:
      require_available(bytes, offset, 4);
      offset += 4;
      return;
   default:
      throw_protobuf("unsupported IPNS protobuf group wire type");
   }
}

void append_optional_bytes(std::vector<std::uint8_t>& out, std::uint32_t field,
                           const std::optional<std::vector<std::uint8_t>>& value) {
   if (value) {
      const auto key = (static_cast<std::uint64_t>(field) << 3U) | 2U;
      require_field_growth(out, varint_size(key), varint_size(value->size()), value->size());
      detail::append_bytes(out, field, *value);
   }
}

void append_optional_uint64(std::vector<std::uint8_t>& out, std::uint32_t field,
                            const std::optional<std::uint64_t>& value) {
   if (value) {
      const auto key = static_cast<std::uint64_t>(field) << 3U;
      require_field_growth(out, varint_size(key), varint_size(*value));
      detail::append_uint64(out, field, *value);
   }
}

} // namespace

std::vector<std::uint8_t> encode(const wire_record& value) {
   auto out = std::vector<std::uint8_t>{};
   append_optional_bytes(out, 1, value.value);
   append_optional_bytes(out, 2, value.signature_v1);
   append_optional_uint64(out, 3, value.validity_type);
   append_optional_bytes(out, 4, value.validity);
   append_optional_uint64(out, 5, value.sequence);
   append_optional_uint64(out, 6, value.ttl);
   append_optional_bytes(out, 7, value.public_key);
   append_optional_bytes(out, 8, value.signature_v2);
   append_optional_bytes(out, 9, value.data);
   require_growth(out, value.unknown_fields.size());
   out.insert(out.end(), value.unknown_fields.begin(), value.unknown_fields.end());
   return out;
}

wire_record decode(std::span<const std::uint8_t> bytes) {
   if (bytes.size() > ipns::max_record_size) {
      throw_protobuf("IPNS protobuf record exceeds 10 KiB");
   }
   auto out = wire_record{};
   auto offset = std::size_t{};
   while (offset < bytes.size()) {
      const auto field_start = offset;
      const auto key = read_varint(bytes, offset);
      const auto field_value = key >> 3U;
      const auto wire = static_cast<std::uint8_t>(key & 0x07U);
      if (field_value == 0 || field_value > (std::numeric_limits<std::uint32_t>::max)()) {
         throw_protobuf("invalid IPNS protobuf field number");
      }
      const auto field = static_cast<std::uint32_t>(field_value);
      const auto require_wire = [&](std::uint8_t expected) {
         if (wire != expected) {
            throw_protobuf("IPNS protobuf field uses an invalid wire type");
         }
      };
      switch (field) {
      case 1:
         require_wire(2);
         out.value = read_bytes(bytes, offset);
         break;
      case 2:
         require_wire(2);
         out.signature_v1 = read_bytes(bytes, offset);
         break;
      case 3:
         require_wire(0);
         out.validity_type = read_varint(bytes, offset);
         break;
      case 4:
         require_wire(2);
         out.validity = read_bytes(bytes, offset);
         break;
      case 5:
         require_wire(0);
         out.sequence = read_varint(bytes, offset);
         break;
      case 6:
         require_wire(0);
         out.ttl = read_varint(bytes, offset);
         break;
      case 7:
         require_wire(2);
         out.public_key = read_bytes(bytes, offset);
         break;
      case 8:
         require_wire(2);
         out.signature_v2 = read_bytes(bytes, offset);
         break;
      case 9:
         require_wire(2);
         out.data = read_bytes(bytes, offset);
         break;
      default:
         skip_unknown(bytes, offset, wire);
         out.unknown_fields.insert(out.unknown_fields.end(), bytes.begin() + static_cast<std::ptrdiff_t>(field_start),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(offset));
         break;
      }
   }
   return out;
}

} // namespace forge::net::p2p::detail::ipns_protobuf
