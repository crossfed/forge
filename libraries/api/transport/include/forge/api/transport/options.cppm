module;

#include <chrono>
#include <cstddef>
#include <cstdint>

export module forge.api.transport.options;

export import forge.api.core.types;

export namespace forge::api::transport {

struct options {
   forge::api::core::codec_id codec{.value = "forge.raw"};
   std::size_t max_inflight = 128;
   std::chrono::milliseconds deadline{0};
   std::uint32_t max_frame_size = 16 * 1024 * 1024;
};

struct call_options {
   forge::api::core::call_id id{};
   forge::api::core::metadata meta;
   std::chrono::milliseconds deadline{0};
};

struct session_options {
   options stream;
   std::size_t max_concurrent_streams = 128;
};

} // namespace forge::api::transport
