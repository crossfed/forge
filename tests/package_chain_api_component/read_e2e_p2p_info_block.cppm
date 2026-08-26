module;

#include <atomic>
#include <cstdint>
#include <memory>

export module package.chain_api_component.read_e2e_p2p_info_block;

export import forge.chain.protocol.block_query;
export import forge.chain.protocol.info;

export namespace package_chain_api_component {

struct read_p2p_info_block_fixture {
   forge::chain::protocol::info_response information;
   forge::chain::protocol::block_state_response block;
   std::atomic<std::uint32_t> information_calls{0};
   std::atomic<std::uint32_t> block_calls{0};
   std::atomic<forge::chain::protocol::audit_mode> information_audit{forge::chain::protocol::audit_mode::none};
   std::atomic<forge::chain::protocol::audit_mode> block_audit{forge::chain::protocol::audit_mode::none};
};

struct read_p2p_info_block_responses {
   forge::chain::protocol::info_response information;
   forge::chain::protocol::block_state_response block;
};

read_p2p_info_block_responses run_p2p_info_block_e2e(std::shared_ptr<read_p2p_info_block_fixture> state);

} // namespace package_chain_api_component
