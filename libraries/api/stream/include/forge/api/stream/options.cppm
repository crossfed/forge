module;

#include <chrono>
#include <cstddef>
#include <cstdint>

export module forge.api.stream.options;

export import forge.api.core.types;

export namespace forge::api::stream {

struct options {
   forge::api::core::codec_id codec{.value = "forge.raw"};
   std::size_t max_inflight = 128;
   std::chrono::milliseconds deadline{0};
   std::uint32_t max_frame_size = 16 * 1024 * 1024;
};

} // namespace forge::api::stream
