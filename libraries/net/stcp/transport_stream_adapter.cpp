module;

#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/compat/move_only_function.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

module forge.net.stcp.connection;

import forge.exceptions;
import forge.net.transport.exceptions;
import forge.net.transport.stream;

#include "details/transport_stream_adapter.hxx"

namespace forge::net::stcp::detail {
namespace {

[[nodiscard]] boost::system::error_code error_from_exception(std::exception_ptr error) noexcept {
   if (!error) {
      return {};
   }
   try {
      std::rethrow_exception(error);
   } catch (const forge::exceptions::base& value) {
      const auto code = forge::net::transport::exceptions::code_of(value);
      if (code == forge::net::transport::exceptions::code::canceled) {
         return boost::asio::error::operation_aborted;
      }
      if (code == forge::net::transport::exceptions::code::closed) {
         return boost::asio::error::eof;
      }
      return boost::asio::error::fault;
   } catch (const boost::system::system_error& value) {
      return value.code();
   } catch (...) {
      return boost::asio::error::fault;
   }
}

} // namespace

transport_stream_adapter::transport_stream_adapter() = default;

transport_stream_adapter::transport_stream_adapter(boost::asio::any_io_executor executor,
                                                   forge::net::transport::stream stream)
    : state_(std::make_shared<state>(std::move(executor), std::move(stream))) {}

transport_stream_adapter::~transport_stream_adapter() = default;
transport_stream_adapter::transport_stream_adapter(transport_stream_adapter&&) noexcept = default;
transport_stream_adapter& transport_stream_adapter::operator=(transport_stream_adapter&&) noexcept = default;

transport_stream_adapter::executor_type transport_stream_adapter::get_executor() const noexcept {
   return state_ ? state_->get_executor() : executor_type{};
}

bool transport_stream_adapter::valid() const noexcept {
   return state_ && state_->valid();
}

transport_stream_adapter& transport_stream_adapter::lowest_layer() noexcept {
   return *this;
}

const transport_stream_adapter& transport_stream_adapter::lowest_layer() const noexcept {
   return *this;
}

void transport_stream_adapter::request_cancel() noexcept {
   if (state_) {
      state_->request_cancel();
   }
}

boost::asio::awaitable<void> transport_stream_adapter::async_close() {
   if (state_) {
      co_await state_->async_close();
   }
}

transport_stream_adapter::state::state(boost::asio::any_io_executor executor, forge::net::transport::stream stream)
    : executor_(std::move(executor)), stream_(std::move(stream)) {}

transport_stream_adapter::state::~state() {
   request_cancel();
}

boost::asio::any_io_executor transport_stream_adapter::state::get_executor() const noexcept {
   return executor_;
}

bool transport_stream_adapter::state::valid() const noexcept {
   return !cancel_requested_.load(std::memory_order_acquire) && stream_.valid();
}

void transport_stream_adapter::state::request_cancel() noexcept {
   if (!cancel_requested_.exchange(true, std::memory_order_acq_rel)) {
      stream_.request_cancel();
   }
}

boost::asio::awaitable<void> transport_stream_adapter::state::async_close() {
   request_cancel();
   try {
      co_await stream_.async_close();
   } catch (...) {
      // Terminal cleanup owns lower close but preserves the primary STCP error.
   }
}

void transport_stream_adapter::state::start_read(std::vector<boost::asio::mutable_buffer> destination,
                                                 completion handler) {
   auto self = shared_from_this();
   boost::asio::co_spawn(
       executor_,
       [self = std::move(self), destination = std::move(destination)]() mutable -> boost::asio::awaitable<std::size_t> {
          co_return co_await self->async_read_some(std::move(destination));
       },
       [handler = std::move(handler)](std::exception_ptr error, std::size_t size) mutable {
          handler(error_from_exception(std::move(error)), size);
       });
}

void transport_stream_adapter::state::start_write(std::vector<boost::asio::const_buffer> source, completion handler) {
   auto self = shared_from_this();
   boost::asio::co_spawn(
       executor_,
       [self = std::move(self), source = std::move(source)]() mutable -> boost::asio::awaitable<std::size_t> {
          co_return co_await self->async_write_some(std::move(source));
       },
       [handler = std::move(handler)](std::exception_ptr error, std::size_t size) mutable {
          handler(error_from_exception(std::move(error)), size);
       });
}

boost::asio::awaitable<std::size_t>
transport_stream_adapter::state::async_read_some(std::vector<boost::asio::mutable_buffer> destination) {
   if (!valid()) {
      FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::closed, "STCP lower transport stream is closed");
   }
   if (boost::asio::buffer_size(destination) == 0) {
      co_return 0;
   }
   while (pending_read_.empty()) {
      pending_read_ = co_await stream_.async_read();
   }
   const auto copied = boost::asio::buffer_copy(destination, boost::asio::buffer(pending_read_));
   pending_read_.erase(pending_read_.begin(), pending_read_.begin() + static_cast<std::ptrdiff_t>(copied));
   co_return copied;
}

boost::asio::awaitable<std::size_t>
transport_stream_adapter::state::async_write_some(std::vector<boost::asio::const_buffer> source) {
   if (!valid()) {
      FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::closed, "STCP lower transport stream is closed");
   }
   const auto size = boost::asio::buffer_size(source);
   if (size == 0) {
      co_return 0;
   }
   auto bytes = std::vector<std::uint8_t>(size);
   static_cast<void>(boost::asio::buffer_copy(boost::asio::buffer(bytes), source));
   co_await stream_.async_write(std::span<const std::uint8_t>{bytes});
   co_return size;
}

} // namespace forge::net::stcp::detail
