module;

#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

module forge.net.p2p.node;

import forge.net.p2p.exceptions;
import forge.net.p2p.resource_manager;
import forge.net.transport.frame;
import forge.net.transport.stream;

#include "details/resource_stream.hxx"

namespace forge::net::p2p::detail {
namespace {

[[nodiscard]] std::shared_ptr<void>
queued_lifetime(resource_manager::queued_bytes_reservation reservation) {
   return std::make_shared<resource_manager::queued_bytes_reservation>(std::move(reservation));
}

[[nodiscard]] std::uint64_t framed_size(std::size_t payload_size) noexcept {
   constexpr auto frame_header_size = std::uint64_t{4};
   if (payload_size > (std::numeric_limits<std::uint64_t>::max)() - frame_header_size) {
      return (std::numeric_limits<std::uint64_t>::max)();
   }
   return static_cast<std::uint64_t>(payload_size) + frame_header_size;
}

} // namespace

resource_stream::resource_stream(forge::net::transport::stream stream, resource_manager manager,
                                 resource_manager::stream_reservation reservation)
    : stream_(std::move(stream)), manager_(std::move(manager)), reservation_(std::move(reservation)) {}

resource_stream::~resource_stream() {
   if (reservation_.active()) {
      stream_.cancel();
      reservation_.release();
   }
}

bool resource_stream::valid() const noexcept {
   return stream_.valid() && reservation_.active();
}

std::int64_t resource_stream::id() const noexcept {
   return stream_.id();
}

bool resource_stream::bind(resource_manager::scope value) noexcept {
   return reservation_.bind(std::move(value));
}

boost::asio::awaitable<void> resource_stream::async_write(std::span<const std::uint8_t> bytes) {
   auto admitted = manager_.reserve_queued_bytes(bytes.size());
   if (!admitted) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P node queued-byte budget exhausted");
   }
   auto owned = forge::net::transport::chunk{bytes};
   forge::net::transport::detail::chunk_access::attach_lifetime(
      owned, queued_lifetime(std::move(*admitted)));
   co_await stream_.async_write(std::move(owned));
}

boost::asio::awaitable<void> resource_stream::async_write_chunk(forge::net::transport::chunk bytes) {
   auto admitted = manager_.reserve_queued_bytes(bytes.size());
   if (!admitted) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P node queued-byte budget exhausted");
   }
   forge::net::transport::detail::chunk_access::attach_lifetime(
      bytes, queued_lifetime(std::move(*admitted)));
   co_await stream_.async_write(std::move(bytes));
}

boost::asio::awaitable<void> resource_stream::async_write_frame(std::span<const std::uint8_t> bytes) {
   auto admitted = manager_.reserve_queued_bytes(framed_size(bytes.size()));
   if (!admitted) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P node queued-byte budget exhausted");
   }
   auto encoded = forge::net::transport::chunk{forge::net::transport::encode_frame(bytes)};
   forge::net::transport::detail::chunk_access::attach_lifetime(
      encoded, queued_lifetime(std::move(*admitted)));
   co_await stream_.async_write(std::move(encoded));
}

boost::asio::awaitable<void>
resource_stream::async_write_frame_chunk(forge::net::transport::chunk bytes) {
   auto admitted = manager_.reserve_queued_bytes(framed_size(bytes.size()));
   if (!admitted) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P node queued-byte budget exhausted");
   }
   auto [payload, source_lifetime] = forge::net::transport::detail::chunk_access::consume(std::move(bytes));
   auto encoded = forge::net::transport::chunk{forge::net::transport::encode_frame(payload)};
   forge::net::transport::detail::chunk_access::attach_lifetime(encoded, std::move(source_lifetime));
   forge::net::transport::detail::chunk_access::attach_lifetime(
      encoded, queued_lifetime(std::move(*admitted)));
   co_await stream_.async_write(std::move(encoded));
}

boost::asio::awaitable<std::vector<std::uint8_t>> resource_stream::async_read() {
   co_return co_await stream_.async_read();
}

boost::asio::awaitable<forge::net::transport::chunk> resource_stream::async_read_chunk() {
   co_return co_await stream_.async_read_chunk();
}

boost::asio::awaitable<void> resource_stream::async_close() {
   try {
      co_await stream_.async_close();
   } catch (...) {
      stream_.cancel();
      reservation_.release();
      throw;
   }
   reservation_.release();
}

void resource_stream::cancel() {
   stream_.cancel();
   reservation_.release();
}

std::pair<forge::net::transport::stream, std::shared_ptr<resource_stream>>
make_resource_stream(forge::net::transport::stream stream, resource_manager manager,
                     resource_manager::stream_reservation reservation) {
   auto resource = std::make_shared<resource_stream>(std::move(stream), std::move(manager), std::move(reservation));
   return {forge::net::transport::detail::stream_access::make(resource), resource};
}

boost::asio::awaitable<void>
async_close_unescaped(const std::shared_ptr<resource_stream>& resource) {
   // The dispatcher keeps one owner so normal handler completion can send a
   // graceful close. A second owner means the facade escaped the handler.
   if (resource && resource.use_count() == 1) {
      co_await resource->async_close();
   }
}

} // namespace forge::net::p2p::detail
