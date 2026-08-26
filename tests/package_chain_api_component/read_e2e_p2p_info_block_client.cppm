module;

#include <boost/asio/awaitable.hpp>

export module package.chain_api_component.read_e2e_p2p_info_block_client;

export import forge.api.transport.connection;
import package.chain_api_component.read_e2e_p2p_info_block;

export namespace package_chain_api_component {

boost::asio::awaitable<read_p2p_info_block_responses>
run_p2p_info_block_client(forge::api::transport::connection connection);

} // namespace package_chain_api_component
