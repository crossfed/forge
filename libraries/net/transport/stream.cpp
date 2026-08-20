module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module forge.net.transport.stream;

import forge.net.transport.exceptions;

#include "details/bounded_frame_buffer.hxx"

namespace forge::net::transport {

struct stream::impl {
   std::shared_ptr<detail::stream_concept> model;
   detail::bounded_frame_buffer buffer;
};

stream::stream() = default;
stream::stream(std::shared_ptr<detail::stream_concept> model) : impl_(std::make_shared<impl>()) {
   impl_->model = std::move(model);
}

stream::~stream() = default;
stream::stream(stream&&) noexcept = default;
stream& stream::operator=(stream&&) noexcept = default;

bool stream::valid() const noexcept {
   return impl_ && impl_->model && impl_->model->valid();
}

std::int64_t stream::id() const noexcept {
   return impl_ && impl_->model ? impl_->model->id() : -1;
}

boost::asio::awaitable<void> stream::async_write(std::span<const std::uint8_t> bytes) {
   if (!impl_ || !impl_->model) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   co_await impl_->model->async_write_chunk(chunk{bytes});
}

boost::asio::awaitable<void> stream::async_write(chunk bytes) {
   if (!impl_ || !impl_->model) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   co_await impl_->model->async_write_chunk(std::move(bytes));
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read() {
   auto value = co_await async_read_chunk();
   co_return std::move(value).into_vector();
}

boost::asio::awaitable<chunk> stream::async_read_chunk() {
   if (!impl_ || !impl_->model) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   if (!impl_->buffer.empty()) {
      co_return impl_->buffer.take_all();
   }
   co_return co_await impl_->model->async_read_chunk();
}

boost::asio::awaitable<void> stream::async_write_frame(std::span<const std::uint8_t> bytes) {
   if (!impl_ || !impl_->model) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   co_await impl_->model->async_write_frame_chunk(chunk{bytes});
}

boost::asio::awaitable<void> stream::async_write_frame(chunk bytes) {
   if (!impl_ || !impl_->model) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   co_await impl_->model->async_write_frame_chunk(std::move(bytes));
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read_frame() {
   co_return co_await async_read_frame(frame_options{});
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read_frame(frame_options options) {
   auto value = co_await async_read_frame_chunk(options);
   co_return std::move(value).into_vector();
}

boost::asio::awaitable<chunk> stream::async_read_frame_chunk() {
   co_return co_await async_read_frame_chunk(frame_options{});
}

boost::asio::awaitable<chunk> stream::async_read_frame_chunk(frame_options options) {
   if (!impl_ || !impl_->model) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   while (true) {
      impl_->buffer.enforce_limit(options);
      const auto decoded = decode_frame_view(impl_->buffer.bytes(), options);
      if (decoded.status == frame_decode_status::complete) {
         co_return impl_->buffer.take_frame_payload(decoded.consumed, decoded.payload.size());
      }
      auto next = co_await impl_->model->async_read_chunk();
      auto view = next.bytes();
      if (view.empty()) {
         continue;
      }
      impl_->buffer.append(view, options);
   }
}

boost::asio::awaitable<void> stream::async_close() {
   if (!impl_ || !impl_->model) {
      co_return;
   }
   co_await impl_->model->async_close();
}

void stream::cancel() {
   if (impl_ && impl_->model) {
      impl_->model->cancel();
   }
}

stream detail::stream_access::make(std::shared_ptr<stream_concept> model) {
   return stream{std::move(model)};
}

stream detail::stream_access::with_buffer(stream value, std::vector<std::uint8_t> buffered) {
   if (!value.impl_ || buffered.empty()) {
      return value;
   }
   value.impl_->buffer.append_prefetched(std::move(buffered));
   return value;
}

boost::asio::awaitable<void> detail::stream_concept::async_write_chunk(chunk bytes) {
   co_await async_write(bytes.bytes());
}

boost::asio::awaitable<void> detail::stream_concept::async_write_frame(std::span<const std::uint8_t> bytes) {
   co_await async_write_frame_chunk(chunk{bytes});
}

boost::asio::awaitable<void> detail::stream_concept::async_write_frame_chunk(chunk bytes) {
   auto [payload, lifetime] = detail::chunk_access::consume(std::move(bytes));
   auto encoded = chunk{encode_frame(payload)};
   detail::chunk_access::attach_lifetime(encoded, std::move(lifetime));
   co_await async_write_chunk(std::move(encoded));
}

boost::asio::awaitable<chunk> detail::stream_concept::async_read_chunk() {
   co_return chunk{co_await async_read()};
}

} // namespace forge::net::transport
