module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <coroutine>
#include <memory>
#include <utility>

module package.chain_api_component.read_e2e_p2p_state_client;

import package.chain_api_component.test_support;
import forge.chain.api.exceptions;
import forge.chain.api.state;
import forge.chain.protocol.audit;
import forge.chain.protocol.state_query;

namespace package_chain_api_component {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

boost::asio::awaitable<read_p2p_state_responses> run_p2p_state_client(forge::api::transport::connection connection,
                                                                      std::shared_ptr<read_p2p_state_fixture> state) {
   auto remote = co_await connection.get_remote_api<chain_api::state>();
   auto responses = read_p2p_state_responses{};
   responses.state = co_await remote->get_table_changes(protocol::table_changes_request{
       .from_block = 39,
       .to_block = 40,
       .tables = {{.code = protocol::account_name{"tester"},
                   .scope = protocol::name{"scope"},
                   .table = protocol::name{"rows"}}},
       .audit = protocol::audit_mode::required,
   });
   const auto calls_before_oversized = state->calls.load(std::memory_order_relaxed);
   try {
      static_cast<void>(co_await remote->get_table_changes(protocol::table_changes_request{
          .from_block = 39,
          .to_block = 40,
          .tables = {{.code = protocol::account_name{"tester"}}},
          .cursor = protocol::bytes(70U * 1024U, 0x5aU),
      }));
   } catch (const forge::chain::api::exceptions::resource_exhausted&) {
      responses.oversized_request_rejected = true;
   }
   require(responses.oversized_request_rejected, "P2P chain API accepted an oversized typed-state request");
   require(state->calls.load(std::memory_order_relaxed) == calls_before_oversized,
           "P2P oversized request reached the owner service");
   co_return responses;
}

} // namespace package_chain_api_component
