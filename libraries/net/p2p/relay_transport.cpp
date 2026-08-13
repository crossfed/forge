module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <optional>
#include <string>
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
#include "details/libp2p_identity_material.hxx"
#include "details/stream_upgrade.hxx"

namespace forge::net::p2p {

boost::asio::awaitable<std::shared_ptr<forge::net::yamux::session>>
upgrade_relay_outbound_session(forge::net::p2p::stream stream, const node::options& options,
                               const libp2p_identity_material& identity, const peer_id& expected_peer) {
   auto upgraded = co_await upgrade_outbound_stream(
       std::move(stream), options, identity,
       options.allow_insecure_test_mode ? std::nullopt : std::make_optional(expected_peer));
   co_return std::move(upgraded.session);
}

boost::asio::awaitable<std::shared_ptr<forge::net::yamux::session>>
upgrade_relay_inbound_session(forge::net::p2p::stream stream, const node::options& options,
                              const libp2p_identity_material& identity, const peer_id& expected_peer) {
   auto upgraded = co_await upgrade_inbound_stream(
       std::move(stream), options, identity,
       options.allow_insecure_test_mode ? std::nullopt : std::make_optional(expected_peer));
   co_return std::move(upgraded.session);
}

} // namespace forge::net::p2p
