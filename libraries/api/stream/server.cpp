module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module forge.api.stream.server;

import forge.raw.raw;
import forge.net.transport.exceptions;
import forge.net.transport.frame;

namespace forge::api::stream {
namespace {

constexpr auto compact_threshold = std::size_t{65'536};

void compact_buffer(std::vector<std::uint8_t>& buffer, std::size_t& consumed) {
   if (consumed == 0) {
      return;
   }
   if (consumed >= buffer.size()) {
      buffer.clear();
      consumed = 0;
      return;
   }
   auto compacted = std::vector<std::uint8_t>{};
   compacted.reserve(buffer.size() - consumed);
   compacted.insert(compacted.end(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed), buffer.end());
   buffer = std::move(compacted);
   consumed = 0;
}

[[nodiscard]] std::span<const std::uint8_t> available_bytes(const std::vector<std::uint8_t>& buffer,
                                                            std::size_t consumed) noexcept {
   if (consumed >= buffer.size()) {
      return {};
   }
   return {buffer.data() + consumed, buffer.size() - consumed};
}

boost::asio::awaitable<forge::net::transport::chunk> read_transport_frame(forge::net::transport::stream& stream,
                                                                          std::vector<std::uint8_t>& buffer,
                                                                          std::size_t& consumed,
                                                                          std::uint32_t max_frame_size) {
   while (true) {
      const auto decoded = forge::net::transport::decode_frame_view(
         available_bytes(buffer, consumed), forge::net::transport::frame_options{.max_size = max_frame_size});
      if (decoded.status == forge::net::transport::frame_decode_status::complete) {
         const auto payload = forge::net::transport::chunk{decoded.payload};
         consumed += decoded.consumed;
         if (consumed >= buffer.size() || consumed > compact_threshold) {
            compact_buffer(buffer, consumed);
         }
         co_return payload;
      }

      compact_buffer(buffer, consumed);
      auto next = co_await stream.async_read_chunk();
      auto view = next.bytes();
      buffer.insert(buffer.end(), view.begin(), view.end());
   }
}

boost::asio::awaitable<void> write_transport_frame(forge::net::transport::stream& stream,
                                                   std::span<const std::uint8_t> payload,
                                                   std::uint32_t max_frame_size) {
   auto encoded = std::vector<std::uint8_t>{};
   forge::net::transport::encode_frame_to(encoded, payload,
                                          forge::net::transport::frame_options{.max_size = max_frame_size});
   co_await stream.async_write(forge::net::transport::chunk{std::move(encoded)});
}

[[nodiscard]] bool is_clean_close(const forge::exceptions::base& error) noexcept {
   return forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::closed) ||
          forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::canceled);
}

} // namespace

boost::asio::awaitable<void> serve_stream(forge::net::transport::stream stream, forge::api::core::binding_plan plan, options value) {
   co_await serve_stream(std::move(stream), std::move(plan), value, {});
}

boost::asio::awaitable<void> serve_stream(forge::net::transport::stream stream, forge::api::core::binding_plan plan, options value,
                                          forge::api::core::metadata trusted_metadata) {
   auto dispatcher = forge::api::core::frame_dispatcher{
      std::move(plan),
      forge::api::core::dispatch_options{
         .codec = value.codec,
         .max_inflight = value.max_inflight,
         .deadline = value.deadline,
         .trusted_metadata = std::move(trusted_metadata),
      }};
   auto buffer = std::vector<std::uint8_t>{};
   auto consumed = std::size_t{0};

   while (true) {
      try {
         auto payload = co_await read_transport_frame(stream, buffer, consumed, value.max_frame_size);
         auto request = forge::raw::unpack<forge::api::core::frame>(payload.to_vector());
         auto responses = co_await dispatcher.dispatch(std::move(request));
         for (const auto& response : responses) {
            auto encoded = forge::api::core::bytes{};
            forge::raw::pack(encoded, response);
            co_await write_transport_frame(stream, encoded, value.max_frame_size);
         }
      } catch (const forge::exceptions::base& error) {
         if (is_clean_close(error)) {
            co_return;
         }
         throw;
      }
   }
}

} // namespace forge::api::stream
