module;

#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

module forge.net.tcp.connection;

import forge.net.transport.stream;

namespace forge::net::tcp {
namespace {

namespace asio = boost::asio;
using asio_tcp = boost::asio::ip::tcp;

[[nodiscard]] std::int64_t next_stream_id() noexcept {
   static auto next = std::atomic<std::int64_t>{1};
   return next.fetch_add(1, std::memory_order_relaxed);
}

[[noreturn]] void throw_invalid_options(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_options, std::move(message));
}

[[noreturn]] void throw_io_error(std::string message, const boost::system::error_code& error) {
   FORGE_THROW_EXCEPTION(exceptions::io_error, std::move(message), forge::exceptions::ctx("reason", error.message()));
}

[[noreturn]] void throw_read_write_error(const boost::system::error_code& error) {
   if (error == boost::asio::error::operation_aborted) {
      FORGE_THROW_EXCEPTION(exceptions::canceled, "tcp connection operation canceled",
                          forge::exceptions::ctx("reason", error.message()));
   }
   if (error == boost::asio::error::eof || error == boost::asio::error::connection_reset ||
       error == boost::asio::error::broken_pipe) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "tcp connection closed", forge::exceptions::ctx("reason", error.message()));
   }
   throw_io_error("tcp connection I/O failed", error);
}

void validate_options(const options& value) {
   if (value.read_chunk_size == 0) {
      throw_invalid_options("tcp read_chunk_size must be greater than zero");
   }
}

[[nodiscard]] transport::endpoint from_asio_endpoint(const asio_tcp::endpoint& endpoint) {
   const auto address = endpoint.address();
   return transport::endpoint{.host_type = address.is_v6() ? transport::endpoint::host_kind::ip6
                                                           : transport::endpoint::host_kind::ip4,
                              .protocol = transport::endpoint::protocol_kind::tcp,
                              .host = address.to_string(),
                              .port = endpoint.port()};
}

void cancel_socket(asio_tcp::socket& socket) noexcept {
   auto ignored = boost::system::error_code{};
   socket.cancel(ignored);
   socket.shutdown(asio_tcp::socket::shutdown_both, ignored);
   socket.close(ignored);
}

enum class socket_state : std::uint8_t {
   active,
   cancel_requested,
   close_requested,
   handed_off,
   closed,
};

class socket_stream final : public transport::detail::stream_concept,
                            public std::enable_shared_from_this<socket_stream> {
 public:
   socket_stream(std::shared_ptr<asio_tcp::socket> socket, asio::strand<asio::any_io_executor> strand,
                 options tcp_options, std::int64_t id)
       : socket_(std::move(socket)), strand_(std::move(strand)), options_(tcp_options), id_(id) {}

   [[nodiscard]] bool valid() const noexcept override {
      return state_.load(std::memory_order_acquire) == socket_state::active;
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return id_;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override {
      auto self = shared_from_this();
      co_await asio::co_spawn(
          strand_,
          [self = std::move(self), bytes]() -> asio::awaitable<void> {
             if (!self->valid()) {
                FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp stream");
             }
             auto error = boost::system::error_code{};
             co_await asio::async_write(*self->socket_, asio::buffer(bytes),
                                        asio::redirect_error(asio::use_awaitable, error));
             if (error) {
                throw_read_write_error(error);
             }
          },
          asio::use_awaitable);
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
      auto self = shared_from_this();
      auto out = std::vector<std::uint8_t>(options_.read_chunk_size);
      const auto size = co_await asio::co_spawn(
          strand_,
          [self = std::move(self), writable = std::span<std::uint8_t>{out}]() -> asio::awaitable<std::size_t> {
             if (!self->valid()) {
                FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp stream");
             }
             auto error = boost::system::error_code{};
             const auto size = co_await self->socket_->async_read_some(
                 asio::buffer(writable), asio::redirect_error(asio::use_awaitable, error));
             if (error) {
                throw_read_write_error(error);
             }
             co_return size;
          },
          asio::use_awaitable);
      out.resize(size);
      co_return out;
   }

   boost::asio::awaitable<transport::chunk> async_read_chunk() override {
      auto builder = pool_.acquire(options_.read_chunk_size);
      auto writable = builder.writable();
      auto self = shared_from_this();
      const auto size = co_await asio::co_spawn(
          strand_,
          [self = std::move(self), writable]() -> asio::awaitable<std::size_t> {
             if (!self->valid()) {
                FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp stream");
             }
             auto error = boost::system::error_code{};
             const auto size = co_await self->socket_->async_read_some(
                 asio::buffer(writable), asio::redirect_error(asio::use_awaitable, error));
             if (error) {
                throw_read_write_error(error);
             }
             co_return size;
          },
          asio::use_awaitable);
      co_return builder.commit(size);
   }

   boost::asio::awaitable<void> async_close() override {
      static_cast<void>(request_terminal(socket_state::close_requested));
      auto self = shared_from_this();
      co_await asio::co_spawn(
          strand_,
          [self = std::move(self)]() -> asio::awaitable<void> {
             self->close_on_owner();
             co_return;
          },
          asio::use_awaitable);
   }

   void cancel() override {
      if (request_terminal(socket_state::cancel_requested)) {
         auto self = shared_from_this();
         asio::post(strand_, [self = std::move(self)] { self->close_on_owner(); });
      }
   }

 private:
   [[nodiscard]] bool request_terminal(socket_state requested) noexcept {
      auto expected = socket_state::active;
      return state_.compare_exchange_strong(expected, requested, std::memory_order_acq_rel,
                                            std::memory_order_acquire);
   }

   void close_on_owner() noexcept {
      auto current = state_.load(std::memory_order_acquire);
      while (current == socket_state::cancel_requested || current == socket_state::close_requested) {
         if (state_.compare_exchange_weak(current, socket_state::closed, std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
            cancel_socket(*socket_);
            return;
         }
      }
   }

   std::shared_ptr<asio_tcp::socket> socket_;
   asio::strand<asio::any_io_executor> strand_;
   options options_;
   transport::buffer_pool pool_;
   std::int64_t id_ = -1;
   std::atomic<socket_state> state_{socket_state::active};
};

[[nodiscard]] transport::stream make_stream(std::shared_ptr<asio_tcp::socket> socket,
                                            asio::strand<asio::any_io_executor> strand, options tcp_options,
                                            std::int64_t id) {
   return transport::detail::stream_access::make(
       std::make_shared<socket_stream>(std::move(socket), std::move(strand), tcp_options, id));
}

} // namespace

struct connection::impl final : std::enable_shared_from_this<connection::impl> {
   impl(asio_tcp::socket socket_value, options tcp_options_value)
       : socket(std::make_shared<asio_tcp::socket>(std::move(socket_value))), tcp_options(tcp_options_value),
         strand(asio::make_strand(socket->get_executor())), id(next_stream_id()) {
      validate_options(tcp_options);
      auto error = boost::system::error_code{};
      local = from_asio_endpoint(socket->local_endpoint(error));
      if (error) {
         throw_io_error("failed to read tcp local endpoint", error);
      }
      remote = from_asio_endpoint(socket->remote_endpoint(error));
      if (error) {
         throw_io_error("failed to read tcp remote endpoint", error);
      }
   }

   [[nodiscard]] bool valid() const noexcept {
      const auto lock = std::scoped_lock{state_mutex};
      return state == socket_state::active;
   }

   [[nodiscard]] transport::endpoint local_endpoint() const {
      if (!valid()) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      return local;
   }

   [[nodiscard]] transport::endpoint remote_endpoint() const {
      if (!valid()) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      return remote;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) {
      auto self = shared_from_this();
      co_await asio::co_spawn(
          strand,
          [self = std::move(self), bytes]() -> asio::awaitable<void> {
             self->claim_operation();
             auto error = boost::system::error_code{};
             try {
                co_await asio::async_write(*self->socket, asio::buffer(bytes),
                                           asio::redirect_error(asio::use_awaitable, error));
             } catch (...) {
                self->release_operation();
                throw;
             }
             self->release_operation();
             if (error) {
                throw_read_write_error(error);
             }
          },
          asio::use_awaitable);
   }

   boost::asio::awaitable<std::size_t> async_read_some(std::span<std::uint8_t> bytes) {
      auto self = shared_from_this();
      co_return co_await asio::co_spawn(
          strand,
          [self = std::move(self), bytes]() -> asio::awaitable<std::size_t> {
             self->claim_operation();
             auto error = boost::system::error_code{};
             auto size = std::size_t{};
             try {
                size = co_await self->socket->async_read_some(
                    asio::buffer(bytes), asio::redirect_error(asio::use_awaitable, error));
             } catch (...) {
                self->release_operation();
                throw;
             }
             self->release_operation();
             if (error) {
                throw_read_write_error(error);
             }
             co_return size;
          },
          asio::use_awaitable);
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() {
      auto out = std::vector<std::uint8_t>(tcp_options.read_chunk_size);
      auto self = shared_from_this();
      const auto size = co_await asio::co_spawn(
          strand,
          [self = std::move(self), writable = std::span<std::uint8_t>{out}]() -> asio::awaitable<std::size_t> {
             self->claim_operation();
             auto error = boost::system::error_code{};
             auto size = std::size_t{};
             try {
                size = co_await self->socket->async_read_some(
                    asio::buffer(writable), asio::redirect_error(asio::use_awaitable, error));
             } catch (...) {
                self->release_operation();
                throw;
             }
             self->release_operation();
             if (error) {
                throw_read_write_error(error);
             }
             co_return size;
          },
          asio::use_awaitable);
      out.resize(size);
      co_return out;
   }

   boost::asio::awaitable<void> async_close() {
      static_cast<void>(request_terminal(socket_state::close_requested));
      auto self = shared_from_this();
      co_await asio::co_spawn(
          strand,
          [self = std::move(self)]() -> asio::awaitable<void> {
             self->close_on_owner();
             co_return;
          },
          asio::use_awaitable);
   }

   void cancel() {
      if (request_terminal(socket_state::cancel_requested)) {
         auto self = shared_from_this();
         asio::post(strand, [self = std::move(self)] { self->close_on_owner(); });
      }
   }

   [[nodiscard]] transport::stream_connection into_transport_stream() {
      if (!valid()) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      auto current = detach_socket();
      auto stream = make_stream(std::move(current), strand, tcp_options, id);
      return transport::stream_connection{.local_endpoint = local,
                                          .remote_endpoint = remote,
                                          .stream = std::move(stream)};
   }

   [[nodiscard]] asio_tcp::socket release_socket() {
      if (!valid()) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      auto current = detach_socket();
      auto out = std::move(*current);
      return out;
   }

   [[nodiscard]] bool request_terminal(socket_state requested) noexcept {
      const auto lock = std::scoped_lock{state_mutex};
      if (state != socket_state::active) {
         return false;
      }
      state = requested;
      return true;
   }

   void close_on_owner() noexcept {
      auto current = std::shared_ptr<asio_tcp::socket>{};
      {
         const auto lock = std::scoped_lock{state_mutex};
         if (state != socket_state::cancel_requested && state != socket_state::close_requested) {
            return;
         }
         state = socket_state::closed;
         current = socket;
      }
      cancel_socket(*current);
   }

   [[nodiscard]] std::shared_ptr<asio_tcp::socket> detach_socket() {
      const auto lock = std::scoped_lock{state_mutex};
      if (state != socket_state::active) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "tcp connection cannot hand off a terminal socket");
      }
      if (active_operations != 0) {
         FORGE_THROW_EXCEPTION(exceptions::io_error, "tcp connection cannot hand off while I/O is active");
      }
      state = socket_state::handed_off;
      return std::move(socket);
   }

   void claim_operation() {
      const auto lock = std::scoped_lock{state_mutex};
      if (state != socket_state::active) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      ++active_operations;
   }

   void release_operation() noexcept {
      const auto lock = std::scoped_lock{state_mutex};
      --active_operations;
   }

   std::shared_ptr<asio_tcp::socket> socket;
   options tcp_options;
   asio::strand<asio::any_io_executor> strand;
   transport::endpoint local;
   transport::endpoint remote;
   std::int64_t id = -1;
   mutable std::mutex state_mutex;
   socket_state state = socket_state::active;
   std::size_t active_operations = 0;
};

connection::connection() = default;
connection::connection(boost::asio::ip::tcp::socket socket, options tcp_options)
    : impl_(std::make_shared<impl>(std::move(socket), tcp_options)) {}
connection::~connection() = default;
connection::connection(connection&&) noexcept = default;
connection& connection::operator=(connection&&) noexcept = default;

bool connection::valid() const noexcept {
   return impl_ && impl_->valid();
}

transport::endpoint connection::local_endpoint() const {
   if (!valid()) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   return impl_->local_endpoint();
}

transport::endpoint connection::remote_endpoint() const {
   if (!valid()) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   return impl_->remote_endpoint();
}

boost::asio::awaitable<void> connection::async_write(std::span<const std::uint8_t> bytes) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   auto state = impl_;
   co_await state->async_write(bytes);
}

boost::asio::awaitable<std::size_t> connection::async_read_some(std::span<std::uint8_t> bytes) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   auto state = impl_;
   co_return co_await state->async_read_some(bytes);
}

boost::asio::awaitable<std::vector<std::uint8_t>> connection::async_read() {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   auto state = impl_;
   co_return co_await state->async_read();
}

boost::asio::awaitable<void> connection::async_close() {
   if (!impl_) {
      co_return;
   }
   auto state = impl_;
   co_await state->async_close();
}

void connection::cancel() {
   if (impl_) {
      impl_->cancel();
   }
}

transport::stream_connection connection::into_transport_stream() && {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   return impl_->into_transport_stream();
}

boost::asio::ip::tcp::socket connection::release_socket() && {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   return impl_->release_socket();
}

} // namespace forge::net::tcp
