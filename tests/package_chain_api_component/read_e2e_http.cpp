module;

#include <cstdint>
#include <string>
#include <utility>

module package.chain_api_component.read_e2e_http;

import package.chain_api_component.read_fixture;
import forge.api.core.binding;
import forge.api.core.registry;
import forge.api.http.binding;
import forge.api.http.proxy;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.chain.api.block;
import forge.chain.api.exceptions;
import forge.chain.api.info;
import forge.chain.api.limits;
import forge.chain.api.state;
import forge.chain.protocol.audit;
import forge.chain.protocol.block_query;
import forge.chain.protocol.info;
import forge.chain.protocol.state_query;
import forge.net.http.base_url;
import forge.net.http.client;
import forge.net.http.router;
import forge.net.http.server;

namespace package_chain_api_component {

namespace {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

} // namespace

read_responses run_http_read_e2e(read_services& services) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::core::registry{};
   apis.install<chain_api::info>(chain_api::limited_descriptor<chain_api::info>(package_limits()),
                                 services.information());
   apis.install<chain_api::block>(chain_api::limited_descriptor<chain_api::block>(package_limits()), services.blocks());
   apis.install<chain_api::state>(chain_api::limited_descriptor<chain_api::state>(package_limits()), services.state());

   auto router = forge::net::http::router{};
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::info>()
                    .build());
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::block>()
                    .build());
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::state>()
                    .build());

   auto server = forge::net::http::server{
       runtime,
       forge::net::http::server_config{.max_request_body_bytes = 1U * 1024U * 1024U},
       std::move(router),
   };
   forge::asio::blocking::run(runtime, server.async_start());
   require(server.port() != 0, "HTTP chain API server did not bind");

   auto responses = read_responses{};
   try {
      auto client = forge::net::http::client{
          runtime,
          forge::net::http::parse_base_url("http://127.0.0.1:" + std::to_string(server.port())),
      };
      auto info_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::info>(client));
      auto block_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::block>(client));
      auto state_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::state>(client));
      responses.information = forge::asio::blocking::run(
          runtime, info_remote->get(protocol::anchored_request{.audit = protocol::audit_mode::required}));
      responses.block = forge::asio::blocking::run(runtime, block_remote->get_block_state(protocol::block_request{
                                                                .num = 40,
                                                                .audit = protocol::audit_mode::required,
                                                            }));
      responses.state =
          forge::asio::blocking::run(runtime, state_remote->get_table_changes(protocol::table_changes_request{
                                                  .from_block = 39,
                                                  .to_block = 40,
                                                  .tables = {{.code = protocol::account_name{"tester"},
                                                              .scope = protocol::name{"scope"},
                                                              .table = protocol::name{"rows"}}},
                                                  .audit = protocol::audit_mode::required,
                                              }));
      const auto calls_before_oversized = services.state_calls();
      try {
         static_cast<void>(
             forge::asio::blocking::run(runtime, state_remote->get_table_changes(protocol::table_changes_request{
                                                     .from_block = 39,
                                                     .to_block = 40,
                                                     .tables = {{.code = protocol::account_name{"tester"}}},
                                                     .cursor = protocol::bytes(70U * 1024U, 0x5aU),
                                                 })));
      } catch (const forge::chain::api::exceptions::resource_exhausted&) {
         responses.oversized_request_rejected = true;
      }
      require(responses.oversized_request_rejected, "HTTP chain API accepted an oversized typed-state request");
      require(services.state_calls() == calls_before_oversized, "HTTP oversized request reached the owner service");
   } catch (...) {
      forge::asio::blocking::run(runtime, server.async_stop());
      throw;
   }
   forge::asio::blocking::run(runtime, server.async_stop());
   require(server.port() == 0, "HTTP chain API server remained bound after async_stop");
   return responses;
}

} // namespace package_chain_api_component
