#pragma once

namespace forge::net::p2p::detail {

void validate_dht_request(const dht::message& request, const peer_id& remote);
void validate_dht_response(const dht::message& request, const dht::message& response);

boost::asio::awaitable<dht::message> async_exchange_dht(forge::net::p2p::stream stream, dht::message request,
                                                        const dht::options& options, boost::asio::io_context& context,
                                                        std::chrono::milliseconds timeout);

boost::asio::awaitable<void> async_send_dht(forge::net::p2p::stream stream, dht::message request,
                                            const dht::options& options, boost::asio::io_context& context,
                                            std::chrono::milliseconds timeout);

} // namespace forge::net::p2p::detail
