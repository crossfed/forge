module;

#if defined(_WIN32)
#include <winsock2.h>
#endif

#include <ares.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/system/error_code.hpp>

#if defined(_WIN32)
#include <boost/asio/generic/datagram_protocol.hpp>
#include <boost/asio/generic/stream_protocol.hpp>
#else
#include <boost/asio/posix/stream_descriptor.hpp>
#endif

module forge.net.dns.resolver;

#include "details/resolver_socket_registry.hxx"
#include "details/resolver_socket_watch.hxx"

namespace forge::net::dns {

resolver_socket_registry::resolver_socket_registry(boost::asio::any_io_executor executor)
    : executor_(std::move(executor)) {}

std::shared_ptr<resolver_socket_watch> resolver_socket_registry::create_or_find(ares_socket_t fd) {
   if (const auto iterator = watches_.find(fd); iterator != watches_.end()) {
      return iterator->second;
   }
   auto watch = std::make_shared<resolver_socket_watch>(executor_, fd, next_generation_++);
   watches_.emplace(fd, watch);
   return watch;
}

std::shared_ptr<resolver_socket_watch> resolver_socket_registry::find(ares_socket_t fd) const {
   const auto iterator = watches_.find(fd);
   return iterator == watches_.end() ? nullptr : iterator->second;
}

std::shared_ptr<resolver_socket_watch> resolver_socket_registry::remove(ares_socket_t fd) noexcept {
   const auto iterator = watches_.find(fd);
   if (iterator == watches_.end()) {
      return nullptr;
   }
   auto watch = std::move(iterator->second);
   watches_.erase(iterator);
   watch->release_borrowed();
   return watch;
}

bool resolver_socket_registry::contains(ares_socket_t fd,
                                        const std::shared_ptr<resolver_socket_watch>& watch) const noexcept {
   const auto iterator = watches_.find(fd);
   return watch && iterator != watches_.end() && iterator->second == watch &&
          iterator->second->generation() == watch->generation();
}

std::vector<std::shared_ptr<resolver_socket_watch>> resolver_socket_registry::snapshot() const {
   auto result = std::vector<std::shared_ptr<resolver_socket_watch>>{};
   result.reserve(watches_.size());
   for (const auto& [_, watch] : watches_) {
      result.push_back(watch);
   }
   return result;
}

void resolver_socket_registry::release(ares_socket_t fd) noexcept {
   static_cast<void>(remove(fd));
}

void resolver_socket_registry::release_all() noexcept {
   auto watches = snapshot();
   watches_.clear();
   for (const auto& watch : watches) {
      watch->release_borrowed();
   }
}

} // namespace forge::net::dns
