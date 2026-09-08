module;

#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/strand.hpp>

module forge.net.dns.resolver;

import forge.asio.notification;

#include "details/resolver_impl.hxx"

namespace forge::net::dns {

resolver::resolver(boost::asio::any_io_executor executor, resolver_options options)
    : impl_(std::make_shared<impl>(std::move(executor), std::move(options))) {}

resolver::~resolver() {
   if (impl_) {
      impl_->request_close();
   }
}

resolver::resolver(resolver&&) noexcept = default;
resolver& resolver::operator=(resolver&& other) noexcept {
   if (this != &other) {
      if (impl_) {
         impl_->request_close();
      }
      impl_ = std::move(other.impl_);
   }
   return *this;
}

boost::asio::awaitable<address_response>
resolver::async_resolve_addresses(std::string name, address_family family, query_options options,
                                  std::stop_token stop) {
   auto state = impl_;
   auto owned_name = std::move(name);
   auto owned_options = options;
   auto owned_stop = stop;
   if (!state) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "dns resolver is closed");
   }
   return impl::async_resolve_addresses(std::move(state), std::move(owned_name), family, std::move(owned_options),
                                        std::move(owned_stop));
}

boost::asio::awaitable<text_response> resolver::async_resolve_txt(std::string name, query_options options,
                                                                    std::stop_token stop) {
   auto state = impl_;
   auto owned_name = std::move(name);
   auto owned_options = options;
   auto owned_stop = stop;
   if (!state) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "dns resolver is closed");
   }
   return impl::async_resolve_txt(std::move(state), std::move(owned_name), std::move(owned_options),
                                  std::move(owned_stop));
}

std::size_t resolver::active_queries() const noexcept {
   return impl_ ? impl_->active_queries() : 0;
}

void resolver::request_cancel() noexcept {
   if (impl_) {
      impl_->request_cancel();
   }
}

boost::asio::awaitable<void> resolver::async_close() {
   auto state = impl_;
   return impl::async_close(std::move(state));
}

} // namespace forge::net::dns
