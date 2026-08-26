module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module package.chain_api_component.read_e2e_p2p_state_client;

export import forge.api.transport.connection;
import package.chain_api_component.read_e2e_p2p_state;

export namespace package_chain_api_component {

boost::asio::awaitable<read_p2p_state_responses> run_p2p_state_client(forge::api::transport::connection connection,
                                                                      std::shared_ptr<read_p2p_state_fixture> state);

} // namespace package_chain_api_component
