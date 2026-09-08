module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

module forge.net.dns.resolver;

import forge.asio.notification;

#include "details/resolver_impl.hxx"

namespace forge::net::dns {
namespace {

inline constexpr auto maximum_in_flight = std::size_t{1024};

[[noreturn]] void throw_invalid_options(std::string_view message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_options, message);
}

} // namespace

void validate(const resolver_options& options) {
   if (options.max_in_flight == 0 || options.max_in_flight > maximum_in_flight) {
      throw_invalid_options("dns max_in_flight must be between 1 and 1024");
   }
   for (const auto& server : options.nameservers) {
      auto error = boost::system::error_code{};
      const auto address = boost::asio::ip::make_address(server.address, error);
      const auto valid_scope_id = std::all_of(server.scope_id.begin(), server.scope_id.end(), [](char value) {
         const auto byte = static_cast<unsigned char>(value);
         return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
                byte == '.' || byte == '_' || byte == '-';
      });
      if (error || server.port == 0 || !valid_scope_id || server.scope_id.find('%') != std::string::npos) {
         throw_invalid_options("dns nameservers must use a numeric address and non-zero port");
      }
      if ((!address.is_v6() || !address.to_v6().is_link_local()) && !server.scope_id.empty()) {
         throw_invalid_options("dns nameserver scope_id is only valid for IPv6 link-local addresses");
      }
      if (address.is_v6() && address.to_v6().is_link_local() && server.scope_id.empty()) {
         throw_invalid_options("dns IPv6 link-local nameservers require a scope_id");
      }
   }
}

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
