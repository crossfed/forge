module;

#include <boost/asio/awaitable.hpp>

#include <coroutine>
#include <memory>
#include <utility>

module package.chain_api_component.write_e2e_p2p_admin_client;

import package.chain_api_component.test_support;
import forge.api.core.exceptions;
import forge.chain.api.admin;
import forge.chain.protocol.admin;

namespace package_chain_api_component {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

boost::asio::awaitable<write_p2p_admin_responses> run_p2p_admin_client(forge::api::transport::connection connection) {
   auto remote = co_await connection.get_remote_api<chain_api::admin>();
   auto responses = write_p2p_admin_responses{};
   responses.administration = co_await remote->producer_status(protocol::admin_query{});
   try {
      static_cast<void>(co_await remote->prune(protocol::prune_request{.through_block = 40, .max_records = 0}));
   } catch (const forge::api::core::exceptions::remote_internal&) {
      responses.internal_error_preserved = true;
   }
   require(responses.internal_error_preserved, "P2P chain API did not preserve remote error semantics");
   co_return responses;
}

} // namespace package_chain_api_component
