module;

#include <boost/asio/awaitable.hpp>

export module forge.api.transport.server;

export import forge.api.core.dispatcher;
export import forge.api.transport.exceptions;
export import forge.api.transport.options;
export import forge.api.stream.server;
export import forge.net.transport.session;
export import forge.net.transport.stream;

export namespace forge::api::transport {

using forge::api::stream::serve_stream;

boost::asio::awaitable<void> serve_session(forge::net::transport::session session, forge::api::core::binding_plan plan,
                                           session_options value = {});

} // namespace forge::api::transport
