module;

#include <boost/asio/awaitable.hpp>

#include <coroutine>
#include <utility>

module package.chain_api_component.read_e2e_p2p_info_block_client;

import forge.chain.api.block;
import forge.chain.api.info;
import forge.chain.protocol.audit;
import forge.chain.protocol.block_query;
import forge.chain.protocol.info;

namespace package_chain_api_component {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

boost::asio::awaitable<read_p2p_info_block_responses>
run_p2p_info_block_client(forge::api::transport::connection connection) {
   auto information = co_await connection.get_remote_api<chain_api::info>();
   auto blocks = co_await connection.get_remote_api<chain_api::block>();
   co_return read_p2p_info_block_responses{
       .information = co_await information->get(protocol::anchored_request{.audit = protocol::audit_mode::required}),
       .block = co_await blocks->get_block_state(
           protocol::block_request{.num = 40, .audit = protocol::audit_mode::required}),
   };
}

} // namespace package_chain_api_component
