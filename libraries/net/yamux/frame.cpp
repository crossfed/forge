module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

module forge.net.yamux.session;

#include "details/frame.hxx"

namespace forge::net::yamux::detail {
namespace {

void append_u32(bytes& out, std::uint32_t value) {
   out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

} // namespace

std::uint32_t load_u32(std::span<const std::uint8_t> value, std::size_t offset) noexcept {
   return (static_cast<std::uint32_t>(value[offset]) << 24U) |
          (static_cast<std::uint32_t>(value[offset + 1]) << 16U) |
          (static_cast<std::uint32_t>(value[offset + 2]) << 8U) |
          static_cast<std::uint32_t>(value[offset + 3]);
}

bytes encode_frame(frame_type type, std::uint16_t flags, std::uint32_t stream_id, std::uint32_t length,
                   std::span<const std::uint8_t> payload) {
   auto out = bytes{};
   out.reserve(header_size + payload.size());
   out.push_back(version);
   out.push_back(static_cast<std::uint8_t>(type));
   out.push_back(static_cast<std::uint8_t>((flags >> 8U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>(flags & 0xffU));
   append_u32(out, stream_id);
   append_u32(out, length);
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

} // namespace forge::net::yamux::detail
