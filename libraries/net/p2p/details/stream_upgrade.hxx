#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

#include <boost/asio/awaitable.hpp>

namespace forge::net::p2p {

class cancellation_latch {
 public:
   void set(std::function<void()> cancel);
   void cancel() noexcept;
   void clear() noexcept;

 private:
   std::mutex mutex_;
   std::function<void()> current_;
   bool canceled_ = false;
};

struct upgraded_session {
   peer_id peer;
   std::shared_ptr<forge::net::yamux::session> session;
};

boost::asio::awaitable<upgraded_session>
upgrade_outbound_stream(forge::net::p2p::stream stream, const node::options& options, std::optional<peer_id> expected_peer);

boost::asio::awaitable<upgraded_session>
upgrade_inbound_stream(forge::net::p2p::stream stream, const node::options& options, std::optional<peer_id> expected_peer);

struct tcp_upgrade_deadline {
   boost::asio::io_context* context = nullptr;
   std::chrono::milliseconds timeout{0};
   std::shared_ptr<cancellation_latch> cancel_current;
};

boost::asio::awaitable<upgraded_session>
upgrade_outbound_tcp(forge::net::tcp::connection connection, const node::options& options, std::optional<peer_id> expected_peer);

boost::asio::awaitable<upgraded_session>
upgrade_inbound_tcp(forge::net::tcp::connection connection, const node::options& options, std::optional<peer_id> expected_peer);

boost::asio::awaitable<upgraded_session> upgrade_outbound_tcp(forge::net::tcp::connection connection,
                                                              const node::options& options,
                                                              std::optional<peer_id> expected_peer,
                                                              tcp_upgrade_deadline deadline);

boost::asio::awaitable<upgraded_session> upgrade_inbound_tcp(forge::net::tcp::connection connection,
                                                             const node::options& options,
                                                             std::optional<peer_id> expected_peer,
                                                             tcp_upgrade_deadline deadline);

} // namespace forge::net::p2p
