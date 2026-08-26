export module package.chain_api_component.read_result;

export import forge.chain.protocol.block_query;
export import forge.chain.protocol.info;
export import forge.chain.protocol.state_query;

export namespace package_chain_api_component {

struct read_responses {
   forge::chain::protocol::info_response information;
   forge::chain::protocol::block_state_response block;
   forge::chain::protocol::table_changes_response state;
   bool oversized_request_rejected = false;
};

} // namespace package_chain_api_component
