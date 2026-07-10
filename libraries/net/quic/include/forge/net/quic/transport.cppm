module;

export module forge.net.quic.transport;

import forge.asio.runtime;
import forge.net.transport.connector;
import forge.net.transport.endpoint;
import forge.net.transport.listener;
import forge.net.transport.registry;
import forge.net.transport.session;
import forge.net.transport.stream;
import forge.net.quic.connection;
import forge.net.quic.connector;
import forge.net.quic.endpoint;
import forge.net.quic.listener;
import forge.net.quic.options;
import forge.net.quic.stream;

export namespace forge::net::quic {

[[nodiscard]] forge::net::transport::limits to_transport_limits(const transport_limits& value);
[[nodiscard]] transport_limits from_transport_limits(const forge::net::transport::limits& value);

[[nodiscard]] forge::net::transport::endpoint to_transport_endpoint(const endpoint& value);
[[nodiscard]] endpoint from_transport_endpoint(const forge::net::transport::endpoint& value);

[[nodiscard]] forge::net::transport::stream as_transport_stream(stream value);
[[nodiscard]] forge::net::transport::session as_transport_session(connection value);

[[nodiscard]] forge::net::transport::session_connector make_session_connector(forge::asio::runtime& runtime,
                                                                       client_options options = {});
[[nodiscard]] forge::net::transport::session_listener make_session_listener(forge::asio::runtime& runtime,
                                                                     forge::net::transport::endpoint local,
                                                                     server_options options = {},
                                                                     forge::net::transport::listen_options listen_options = {});

void register_session(forge::net::transport::registry& registry, forge::asio::runtime& runtime,
                      client_options client = {}, server_options server = {});

} // namespace forge::net::quic
