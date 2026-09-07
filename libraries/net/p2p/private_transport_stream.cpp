module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module forge.net.p2p.node;

import forge.exceptions;
import forge.net.pnet.protector;
import forge.net.transport.exceptions;
import forge.net.transport.stream;

#include "details/private_transport_stream.hxx"

namespace forge::net::p2p::detail {
namespace {

[[noreturn]] void rethrow_as_transport(const forge::exceptions::base& error) {
   if (const auto code = forge::net::pnet::exceptions::code_of(error)) {
      switch (*code) {
      case forge::net::pnet::exceptions::code::closed:
         FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::closed, error.what());
      case forge::net::pnet::exceptions::code::canceled:
         FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::canceled, error.what());
      case forge::net::pnet::exceptions::code::invalid_options:
         break;
      }
   }
   throw;
}

} // namespace

private_transport_stream::private_transport_stream(forge::net::transport::stream stream)
    : stream_(std::move(stream)) {}

private_transport_stream::~private_transport_stream() = default;

bool private_transport_stream::valid() const noexcept {
   return stream_.valid();
}

std::int64_t private_transport_stream::id() const noexcept {
   return stream_.id();
}

boost::asio::awaitable<void> private_transport_stream::async_write(std::span<const std::uint8_t> bytes) {
   try {
      co_await stream_.async_write(bytes);
   } catch (const forge::exceptions::base& error) {
      rethrow_as_transport(error);
   }
}

boost::asio::awaitable<std::vector<std::uint8_t>> private_transport_stream::async_read() {
   try {
      co_return co_await stream_.async_read();
   } catch (const forge::exceptions::base& error) {
      rethrow_as_transport(error);
   }
}

boost::asio::awaitable<void> private_transport_stream::async_close() {
   try {
      co_await stream_.async_close();
   } catch (const forge::exceptions::base& error) {
      rethrow_as_transport(error);
   }
}

void private_transport_stream::cancel() {
   stream_.request_cancel();
}

forge::net::transport::stream adapt_private_transport_stream(forge::net::transport::stream stream) {
   auto model = std::make_shared<private_transport_stream>(std::move(stream));
   auto weak = std::weak_ptr<private_transport_stream>{model};
   auto cancel = [weak]() noexcept {
      if (const auto value = weak.lock()) {
         value->cancel();
      }
   };
   return forge::net::transport::detail::stream_access::make_cancelable(std::move(model), cancel, std::move(cancel));
}

} // namespace forge::net::p2p::detail
