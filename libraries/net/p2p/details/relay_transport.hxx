#pragma once

namespace forge::net::p2p {

struct libp2p_identity_material;

void trace_relay(std::string_view message);

class relay_secure_io;

boost::asio::awaitable<std::shared_ptr<forge::net::yamux::session>>
upgrade_relay_outbound_session(forge::net::p2p::stream stream, const node::options& options,
                               const libp2p_identity_material& identity, const peer_id& expected_peer);

boost::asio::awaitable<std::shared_ptr<forge::net::yamux::session>>
upgrade_relay_inbound_session(forge::net::p2p::stream stream, const node::options& options,
                              const libp2p_identity_material& identity, const peer_id& expected_peer);

} // namespace forge::net::p2p
