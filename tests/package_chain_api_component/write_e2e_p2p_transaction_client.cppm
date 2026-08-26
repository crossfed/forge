module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module package.chain_api_component.write_e2e_p2p_transaction_client;

export import forge.api.transport.connection;
import package.chain_api_component.write_e2e_p2p_transaction;

export namespace package_chain_api_component {

boost::asio::awaitable<write_p2p_transaction_responses>
run_p2p_transaction_client(forge::api::transport::connection connection,
                           std::shared_ptr<write_p2p_transaction_fixture> state);

} // namespace package_chain_api_component
