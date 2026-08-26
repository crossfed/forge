module;

#include <memory>

export module package.chain_api_component.read_e2e_p2p_state_server;

export import package.chain_api_component.p2p_runtime;
import package.chain_api_component.read_e2e_p2p_state;

export namespace package_chain_api_component {

p2p_publication_callbacks make_p2p_state_publication(std::shared_ptr<read_p2p_state_fixture> state);

} // namespace package_chain_api_component
