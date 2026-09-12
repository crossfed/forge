module;

#if defined(_WIN32)
#include <winsock2.h>
#endif

#include <ares.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/system/error_code.hpp>

#if defined(_WIN32)
#include <boost/asio/generic/datagram_protocol.hpp>
#include <boost/asio/generic/stream_protocol.hpp>
#else
#include <boost/asio/posix/stream_descriptor.hpp>
#endif

#include <forge/exceptions/macros.hpp>

module forge.net.dns.resolver;

#include "details/resolver_socket_watch.hxx"

namespace forge::net::dns {
namespace {

namespace asio = boost::asio;

#if defined(_WIN32)
[[noreturn]] void throw_socket_error() {
   FORGE_THROW_EXCEPTION(exceptions::internal, "c-ares provided an unsupported socket for DNS readiness watching");
}
#endif

} // namespace

resolver_socket_watch::resolver_socket_watch(asio::any_io_executor executor, ares_socket_t fd, std::uint64_t generation)
    : fd_(fd), generation_(generation)
#if !defined(_WIN32)
      , descriptor_(std::move(executor))
#endif
{
#if defined(_WIN32)
   const auto native = static_cast<SOCKET>(fd);
   auto socket_type = 0;
   auto socket_type_size = static_cast<int>(sizeof(socket_type));
   if (::getsockopt(native, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&socket_type), &socket_type_size) == SOCKET_ERROR) {
      throw_socket_error();
   }

   auto address = sockaddr_storage{};
   auto address_size = static_cast<int>(sizeof(address));
   if (::getsockname(native, reinterpret_cast<sockaddr*>(&address), &address_size) == SOCKET_ERROR) {
      throw_socket_error();
   }

   if (socket_type == SOCK_STREAM) {
      stream_socket_ = std::make_unique<asio::generic::stream_protocol::socket>(std::move(executor));
      stream_socket_->assign(asio::generic::stream_protocol{address.ss_family, 0}, native);
   } else if (socket_type == SOCK_DGRAM) {
      datagram_socket_ = std::make_unique<asio::generic::datagram_protocol::socket>(std::move(executor));
      datagram_socket_->assign(asio::generic::datagram_protocol{address.ss_family, 0}, native);
   } else {
      throw_socket_error();
   }
#else
   descriptor_.assign(fd);
#endif
}

resolver_socket_watch::~resolver_socket_watch() {
   release_borrowed();
}

ares_socket_t resolver_socket_watch::fd() const noexcept {
   return fd_;
}

std::uint64_t resolver_socket_watch::generation() const noexcept {
   return generation_;
}

void resolver_socket_watch::set_interest(bool readable, bool writable) noexcept {
   read_enabled_ = readable;
   write_enabled_ = writable;
}

std::optional<std::uint64_t> resolver_socket_watch::begin_read_wait() noexcept {
   if (!read_enabled_ || read_pending_) {
      return std::nullopt;
   }
   read_pending_ = true;
   return ++read_wait_generation_;
}

std::optional<std::uint64_t> resolver_socket_watch::begin_write_wait() noexcept {
   if (!write_enabled_ || write_pending_) {
      return std::nullopt;
   }
   write_pending_ = true;
   return ++write_wait_generation_;
}

bool resolver_socket_watch::complete_read_wait(std::uint64_t generation) noexcept {
   if (!read_pending_ || generation != read_wait_generation_) {
      return false;
   }
   read_pending_ = false;
   return true;
}

bool resolver_socket_watch::complete_write_wait(std::uint64_t generation) noexcept {
   if (!write_pending_ || generation != write_wait_generation_) {
      return false;
   }
   write_pending_ = false;
   return true;
}

void resolver_socket_watch::async_wait_read(std::function<void(const boost::system::error_code&)> handler) {
#if defined(_WIN32)
   if (stream_socket_) {
      stream_socket_->async_wait(asio::socket_base::wait_read, std::move(handler));
   } else if (datagram_socket_) {
      datagram_socket_->async_wait(asio::socket_base::wait_read, std::move(handler));
   } else {
      handler(asio::error::operation_aborted);
   }
#else
   descriptor_.async_wait(asio::posix::stream_descriptor::wait_read, std::move(handler));
#endif
}

void resolver_socket_watch::async_wait_write(std::function<void(const boost::system::error_code&)> handler) {
#if defined(_WIN32)
   if (stream_socket_) {
      stream_socket_->async_wait(asio::socket_base::wait_write, std::move(handler));
   } else if (datagram_socket_) {
      datagram_socket_->async_wait(asio::socket_base::wait_write, std::move(handler));
   } else {
      handler(asio::error::operation_aborted);
   }
#else
   descriptor_.async_wait(asio::posix::stream_descriptor::wait_write, std::move(handler));
#endif
}

void resolver_socket_watch::release_borrowed() noexcept {
#if defined(_WIN32)
   auto ignored = boost::system::error_code{};
   if (stream_socket_) {
      stream_socket_->cancel(ignored);
      static_cast<void>(stream_socket_->release(ignored));
      stream_socket_.reset();
   }
   if (datagram_socket_) {
      datagram_socket_->cancel(ignored);
      static_cast<void>(datagram_socket_->release(ignored));
      datagram_socket_.reset();
   }
#else
   if (!descriptor_.is_open()) {
      return;
   }
   auto ignored = boost::system::error_code{};
   descriptor_.cancel(ignored);
   static_cast<void>(descriptor_.release());
#endif
}

} // namespace forge::net::dns
