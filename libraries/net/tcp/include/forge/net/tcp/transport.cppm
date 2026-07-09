module;

#include <boost/asio/any_io_executor.hpp>

export module forge.net.tcp.transport;

export import forge.net.tcp.connector;
export import forge.net.tcp.listener;
export import forge.net.transport.registry;

export namespace forge::net::tcp {

void register_stream(transport::registry& registry, boost::asio::any_io_executor executor, options tcp_options = {});

} // namespace forge::net::tcp
