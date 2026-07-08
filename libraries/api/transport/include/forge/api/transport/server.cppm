module;

#include <boost/asio/awaitable.hpp>

export module forge.api.transport.server;

export import forge.api.core.dispatcher;
export import forge.api.transport.exceptions;
export import forge.api.transport.options;
export import forge.net.transport.session;
export import forge.net.transport.stream;

export namespace forge::api::transport {

boost::asio::awaitable<void> serve_stream(forge::net::transport::stream stream, forge::api::core::binding_plan plan,
                                          options value = {});
boost::asio::awaitable<void> serve_stream(forge::net::transport::stream stream, forge::api::core::binding_plan plan, options value,
                                          forge::api::core::metadata trusted_metadata);
boost::asio::awaitable<void> serve_session(forge::net::transport::session session, forge::api::core::binding_plan plan,
                                           session_options value = {});

} // namespace forge::api::transport
