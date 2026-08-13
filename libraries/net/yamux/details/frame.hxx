#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace forge::net::yamux::detail {

using bytes = std::vector<std::uint8_t>;

enum class frame_type : std::uint8_t {
   data = 0,
   window_update = 1,
   ping = 2,
   go_away = 3,
};

inline constexpr std::uint8_t version = 0;
inline constexpr std::uint16_t syn = 0x01;
inline constexpr std::uint16_t ack = 0x02;
inline constexpr std::uint16_t fin = 0x04;
inline constexpr std::uint16_t rst = 0x08;
inline constexpr std::uint16_t known_flags = syn | ack | fin | rst;
inline constexpr std::size_t header_size = 12;
inline constexpr std::uint32_t initial_stream_window = 256U * 1024U;
inline constexpr std::uint32_t go_away_normal = 0;
inline constexpr std::uint32_t go_away_protocol = 1;
inline constexpr std::uint32_t go_away_internal = 2;

inline constexpr std::uint32_t maximum_stream_id = 0xffff'ffffU;

[[nodiscard]] constexpr bool can_advance_stream_id(std::uint32_t current) noexcept {
   return current <= maximum_stream_id - 4U;
}

static_assert(can_advance_stream_id(1U));
static_assert(can_advance_stream_id(maximum_stream_id - 4U));
static_assert(!can_advance_stream_id(maximum_stream_id - 3U));
static_assert(!can_advance_stream_id(maximum_stream_id - 2U));
static_assert(!can_advance_stream_id(maximum_stream_id));

struct frame_header {
   frame_type type = frame_type::data;
   std::uint16_t flags = 0;
   std::uint32_t stream_id = 0;
   std::uint32_t length = 0;
};

[[nodiscard]] std::uint32_t load_u32(std::span<const std::uint8_t> value, std::size_t offset) noexcept;
[[nodiscard]] bytes encode_frame(frame_type type, std::uint16_t flags, std::uint32_t stream_id,
                                 std::uint32_t length, std::span<const std::uint8_t> payload = {});

} // namespace forge::net::yamux::detail
