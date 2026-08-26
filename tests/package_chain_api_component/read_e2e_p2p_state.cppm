module;

#include <atomic>
#include <cstdint>
#include <memory>

export module package.chain_api_component.read_e2e_p2p_state;

export import forge.chain.protocol.state_query;

export namespace package_chain_api_component {

struct read_p2p_state_fixture {
   forge::chain::protocol::table_changes_response response;
   std::atomic<std::uint32_t> calls{0};
   std::atomic<forge::chain::protocol::audit_mode> audit{forge::chain::protocol::audit_mode::none};
};

struct read_p2p_state_responses {
   forge::chain::protocol::table_changes_response state;
   bool oversized_request_rejected = false;
};

read_p2p_state_responses run_p2p_state_e2e(std::shared_ptr<read_p2p_state_fixture> state);

} // namespace package_chain_api_component
