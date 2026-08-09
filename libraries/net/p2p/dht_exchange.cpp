module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

module forge.net.p2p.node;

import forge.multiformats.exceptions;
import forge.multiformats.varint;
import forge.net.p2p.dht;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.stream;

#include "details/dht_exchange.hxx"
#include "details/operation_deadline.hxx"

namespace forge::net::p2p::detail {
namespace {

boost::asio::awaitable<std::vector<std::uint8_t>> async_read_dht_message(forge::net::p2p::stream& stream,
                                                                         std::size_t max_payload_size) {
   auto buffer = std::vector<std::uint8_t>{};
   while (true) {
      try {
         const auto decoded = forge::multiformats::varint_decode(buffer);
         if (decoded.value > max_payload_size) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "libp2p DHT message exceeds max size");
         }
         const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
         if (buffer.size() >= total) {
            co_return std::vector<std::uint8_t>{buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total)};
         }
      } catch (const forge::multiformats::exceptions::invalid_format& error) {
         if (std::string_view{error.what()}.find("unterminated") == std::string_view::npos) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, error.what());
         }
      }
      auto chunk = co_await stream.async_read();
      buffer.insert(buffer.end(), chunk.begin(), chunk.end());
   }
}

} // namespace

void validate_dht_request(const dht::message& request, const peer_id& remote) {
   if (request.type == dht::message_type::put_value || request.type == dht::message_type::get_value) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "DHT value operations require a validated value store");
   }
   if (request.record_value) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT request contains an unexpected value record");
   }
   if (!request.closer_peers.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT request contains unexpected closer peers");
   }
   if (request.type != dht::message_type::add_provider && !request.provider_peers.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT request contains unexpected provider peers");
   }
   if (request.type == dht::message_type::add_provider &&
       std::ranges::any_of(request.provider_peers, [&](const auto& provider) { return provider.id != remote; })) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT provider does not match the authenticated peer");
   }
}

void validate_dht_response(const dht::message& request, const dht::message& response) {
   if (response.type != request.type || response.key_value != request.key_value) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT response does not match its request");
   }
   if (response.record_value) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT response contains an unexpected value record");
   }
   if (request.type == dht::message_type::find_node && !response.provider_peers.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT FIND_NODE response contains provider peers");
   }
   if (request.type == dht::message_type::ping &&
       (!response.closer_peers.empty() || !response.provider_peers.empty())) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "DHT PING response contains unexpected peers");
   }
}

boost::asio::awaitable<dht::message> async_exchange_dht(forge::net::p2p::stream stream, dht::message request,
                                                        const dht::options& options, boost::asio::io_context& context,
                                                        std::chrono::milliseconds timeout) {
   auto deadline = operation_deadline{context, timeout};
   deadline.arm([&stream] { stream.cancel(); });
   try {
      co_await stream.async_write(dht::codec::encode(request, options));
      auto response = dht::codec::decode(co_await async_read_dht_message(stream, options.max_message_size), options);
      validate_dht_response(request, response);
      co_await stream.async_close();
      if (!deadline.finish()) {
         throw_operation_timeout("P2P DHT exchange");
      }
      co_return response;
   } catch (...) {
      const auto completed = deadline.finish();
      stream.cancel();
      if (deadline.timed_out() || !completed) {
         throw_operation_timeout("P2P DHT exchange");
      }
      throw;
   }
}

boost::asio::awaitable<void> async_send_dht(forge::net::p2p::stream stream, dht::message request,
                                            const dht::options& options, boost::asio::io_context& context,
                                            std::chrono::milliseconds timeout) {
   auto deadline = operation_deadline{context, timeout};
   deadline.arm([&stream] { stream.cancel(); });
   try {
      co_await stream.async_write(dht::codec::encode(request, options));
      co_await stream.async_close();
      if (!deadline.finish()) {
         throw_operation_timeout("P2P DHT send");
      }
   } catch (...) {
      const auto completed = deadline.finish();
      stream.cancel();
      if (deadline.timed_out() || !completed) {
         throw_operation_timeout("P2P DHT send");
      }
      throw;
   }
}

} // namespace forge::net::p2p::detail
