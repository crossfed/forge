#include <chrono>
#include <optional>
#include <stop_token>
#include <type_traits>

#include <boost/asio/awaitable.hpp>

import forge.net.stcp.connection;

using generic_client_upgrade = boost::asio::awaitable<forge::net::stcp::connection> (*) (
    forge::net::transport::stream_connection, forge::net::stcp::client_options,
    std::optional<std::chrono::milliseconds>, std::stop_token);
using generic_server_upgrade = boost::asio::awaitable<forge::net::stcp::connection> (*) (
    forge::net::transport::stream_connection, forge::net::stcp::server_options,
    std::optional<std::chrono::milliseconds>, std::stop_token);

static_assert(std::is_same_v<decltype(static_cast<generic_client_upgrade>(&forge::net::stcp::async_upgrade_client)),
                             generic_client_upgrade>);
static_assert(std::is_same_v<decltype(static_cast<generic_server_upgrade>(&forge::net::stcp::async_upgrade_server)),
                             generic_server_upgrade>);

int main() {
   const auto generic_client = static_cast<generic_client_upgrade>(&forge::net::stcp::async_upgrade_client);
   const auto generic_server = static_cast<generic_server_upgrade>(&forge::net::stcp::async_upgrade_server);
   const auto options = forge::net::stcp::client_options{};
   return generic_client != nullptr && generic_server != nullptr && options.read_chunk_size != 0 ? 0 : 1;
}
