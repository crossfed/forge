module;

#include <atomic>
#include <cstdint>
#include <memory>

export module package.chain_api_component.write_e2e_p2p_admin;

export import forge.chain.protocol.admin;

export namespace package_chain_api_component {

struct write_p2p_admin_fixture {
   forge::chain::protocol::producer_status_response response;
   std::atomic<std::uint32_t> calls{0};
   std::atomic<std::uint32_t> error_calls{0};
};

struct write_p2p_admin_responses {
   forge::chain::protocol::producer_status_response administration;
   bool internal_error_preserved = false;
};

write_p2p_admin_responses run_p2p_admin_e2e(std::shared_ptr<write_p2p_admin_fixture> state);

} // namespace package_chain_api_component
