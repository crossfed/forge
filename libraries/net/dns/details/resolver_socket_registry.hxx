#pragma once

#if defined(_WIN32)
#include <winsock2.h>
#endif

#include <ares.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <boost/asio/any_io_executor.hpp>

namespace forge::net::dns {

class resolver_socket_watch;

class resolver_socket_registry final {
 public:
   explicit resolver_socket_registry(boost::asio::any_io_executor executor);

   [[nodiscard]] std::shared_ptr<resolver_socket_watch> create_or_find(ares_socket_t fd);
   [[nodiscard]] std::shared_ptr<resolver_socket_watch> find(ares_socket_t fd) const;
   [[nodiscard]] std::shared_ptr<resolver_socket_watch> remove(ares_socket_t fd) noexcept;
   [[nodiscard]] bool contains(ares_socket_t fd, const std::shared_ptr<resolver_socket_watch>& watch) const noexcept;
   [[nodiscard]] std::vector<std::shared_ptr<resolver_socket_watch>> snapshot() const;
   void release(ares_socket_t fd) noexcept;
   void release_all() noexcept;

 private:
   boost::asio::any_io_executor executor_;
   std::unordered_map<ares_socket_t, std::shared_ptr<resolver_socket_watch>> watches_;
   std::uint64_t next_generation_ = 1;
};

} // namespace forge::net::dns
