export module package.chain_api_component.write_result;

export import forge.chain.protocol.admin;
export import forge.chain.protocol.transaction_query;

export namespace package_chain_api_component {

struct write_responses {
   forge::chain::protocol::transaction_read_only_response transaction;
   forge::chain::protocol::producer_status_response administration;
   bool internal_error_preserved = false;
};

} // namespace package_chain_api_component
