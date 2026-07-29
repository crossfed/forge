module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>

module forge.net.p2p.node;

import forge.crypto.asymmetric;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.stream;
import forge.net.tcp.connection;
import forge.net.yamux.session;

#include "details/relay_transport.hxx"
#include "details/node_identity.hxx"
#include "details/stream_upgrade.hxx"

namespace forge::net::p2p {

void trace_relay(std::string_view message) {
   (void)message;
}

boost::asio::awaitable<std::shared_ptr<forge::net::yamux::session>>
upgrade_relay_outbound_session(forge::net::p2p::stream stream, const node::options& options,
                               const libp2p_identity_material& identity, const peer_id& expected_peer) {
   trace_relay("outbound upgrade: select noise");
   auto upgraded = co_await upgrade_outbound_stream(
       std::move(stream), options, identity,
       options.allow_insecure_test_mode ? std::nullopt : std::make_optional(expected_peer));
   trace_relay("outbound upgrade: yamux ready");
   co_return std::move(upgraded.session);
}

boost::asio::awaitable<std::shared_ptr<forge::net::yamux::session>>
upgrade_relay_inbound_session(forge::net::p2p::stream stream, const node::options& options,
                              const libp2p_identity_material& identity, const peer_id& expected_peer) {
   trace_relay("inbound upgrade: accept noise");
   auto upgraded = co_await upgrade_inbound_stream(
       std::move(stream), options, identity,
       options.allow_insecure_test_mode ? std::nullopt : std::make_optional(expected_peer));
   trace_relay("inbound upgrade: yamux ready");
   co_return std::move(upgraded.session);
}

} // namespace forge::net::p2p
