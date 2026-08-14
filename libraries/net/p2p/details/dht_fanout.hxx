#pragma once

namespace forge::net::p2p::detail::dht_fanout {

struct request {
   std::vector<peer_id> peers;
   std::size_t concurrency = 0;
   std::size_t success_target = 0;
   std::chrono::milliseconds timeout{};
   std::string operation;
};

struct result {
   std::size_t succeeded = 0;
   std::size_t attempted = 0;
};

using operation = std::function<boost::asio::awaitable<bool>(const peer_id&, std::chrono::milliseconds)>;

boost::asio::awaitable<result> run(boost::asio::io_context& context, request value, operation invoke);

} // namespace forge::net::p2p::detail::dht_fanout
