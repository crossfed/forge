#pragma once

#include <ares.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/system/error_code.hpp>

#if defined(_WIN32)
#include <boost/asio/generic/datagram_protocol.hpp>
#include <boost/asio/generic/stream_protocol.hpp>
#else
#include <boost/asio/posix/stream_descriptor.hpp>
#endif

namespace forge::net::dns {

class resolver_socket_watch final {
 public:
   resolver_socket_watch(boost::asio::any_io_executor executor, ares_socket_t fd, std::uint64_t generation);
   ~resolver_socket_watch();

   [[nodiscard]] ares_socket_t fd() const noexcept;
   [[nodiscard]] std::uint64_t generation() const noexcept;
   void set_interest(bool readable, bool writable) noexcept;
   [[nodiscard]] std::optional<std::uint64_t> begin_read_wait() noexcept;
   [[nodiscard]] std::optional<std::uint64_t> begin_write_wait() noexcept;
   [[nodiscard]] bool complete_read_wait(std::uint64_t generation) noexcept;
   [[nodiscard]] bool complete_write_wait(std::uint64_t generation) noexcept;
   void async_wait_read(std::function<void(const boost::system::error_code&)> handler);
   void async_wait_write(std::function<void(const boost::system::error_code&)> handler);
   void release_borrowed() noexcept;

 private:
   const ares_socket_t fd_;
   const std::uint64_t generation_;
#if defined(_WIN32)
   std::unique_ptr<boost::asio::generic::stream_protocol::socket> stream_socket_;
   std::unique_ptr<boost::asio::generic::datagram_protocol::socket> datagram_socket_;
#else
   boost::asio::posix::stream_descriptor descriptor_;
#endif
   bool read_enabled_ = false;
   bool write_enabled_ = false;
   bool read_pending_ = false;
   bool write_pending_ = false;
   std::uint64_t read_wait_generation_ = 0;
   std::uint64_t write_wait_generation_ = 0;
};

} // namespace forge::net::dns
