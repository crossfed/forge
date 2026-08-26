module;

#include <memory>

export module package.chain_api_component.write_e2e_p2p_transaction_server;

export import package.chain_api_component.p2p_runtime;
import package.chain_api_component.write_e2e_p2p_transaction;

export namespace package_chain_api_component {

p2p_publication_callbacks make_p2p_transaction_publication(std::shared_ptr<write_p2p_transaction_fixture> state);

} // namespace package_chain_api_component
