module;

#include <atomic>
#include <cstdint>
#include <memory>

export module package.chain_api_component.write_e2e_p2p_transaction;

export import forge.chain.protocol.transaction_query;

export namespace package_chain_api_component {

struct write_p2p_transaction_fixture {
   forge::chain::protocol::transaction_read_only_response response;
   std::atomic<std::uint32_t> calls{0};
   std::atomic<forge::chain::protocol::audit_mode> audit{forge::chain::protocol::audit_mode::none};
   std::atomic<std::uint32_t> await_started{0};
   std::atomic<std::uint32_t> await_deadlines{0};
   std::atomic<std::uint32_t> await_cancellations{0};
   std::atomic<std::uint32_t> submission_calls{0};
   std::atomic<std::uint64_t> last_submit_timeout_ms{0};
   std::atomic<std::uint64_t> last_batch_timeout_ms{0};
};

struct write_p2p_transaction_responses {
   forge::chain::protocol::transaction_read_only_response transaction;
};

write_p2p_transaction_responses run_p2p_transaction_e2e(std::shared_ptr<write_p2p_transaction_fixture> state);

} // namespace package_chain_api_component
