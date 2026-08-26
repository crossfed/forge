module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module package.chain_api_component.write_e2e_p2p_admin_client;

export import forge.api.transport.connection;
import package.chain_api_component.write_e2e_p2p_admin;

export namespace package_chain_api_component {

boost::asio::awaitable<write_p2p_admin_responses> run_p2p_admin_client(forge::api::transport::connection connection);

} // namespace package_chain_api_component
