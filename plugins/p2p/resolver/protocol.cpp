module;

#include <forge/api/core/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.p2p.resolver.plugin;

import forge.api.core.binding;
import forge.api.core.registry;
import forge.api.transport.connection;
import forge.api.transport.options;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;
import forge.plugins.p2p.resolver.api;
import forge.plugins.p2p.resolver.types;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.types;

#include "details/descriptor_projection.hxx"
#include "details/plugin_impl.hxx"

namespace forge::plugins::p2p::resolver::detail {

class resolver_protocol
    : public forge::api::core::contract<resolver_protocol, forge::api::core::surface::local | forge::api::core::surface::remote> {
 public:
   virtual ~resolver_protocol() = default;
   virtual boost::asio::awaitable<response> query(::forge::plugins::p2p::resolver::query request) = 0;
};

} // namespace forge::plugins::p2p::resolver::detail

FORGE_API(::forge::plugins::p2p::resolver::detail::resolver_protocol,
        FORGE_API_CONTRACT("forge.plugins.p2p.resolver.protocol", 1, 0), FORGE_API_METHOD(query))

namespace forge::plugins::p2p::resolver {

class plugin::resolver_protocol_service final : public detail::resolver_protocol {
 public:
   explicit resolver_protocol_service(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

   boost::asio::awaitable<response> query(::forge::plugins::p2p::resolver::query request) override {
      co_return impl_->query_local(request);
   }

 private:
   std::shared_ptr<plugin::impl> impl_;
};

void plugin::impl::install_protocol() {
   protocol_registry.clear();
   protocol_registry.install<detail::resolver_protocol>(
      std::make_shared<plugin::resolver_protocol_service>(shared_from_this()));
   auto plan = forge::api::core::binding()
                  .serve(protocol_registry)
                  .export_api<detail::resolver_protocol>({.id = {resolver_api_id}, .major = 1, .min_revision = 0})
                  .build();
   p2p->publish_api(std::move(plan), protocol, resolver_transport);
}

boost::asio::awaitable<std::vector<entry>>
plugin::impl::query_remote_apis(forge::net::p2p::peer_id peer, resolve_options options) {
   auto remote = co_await p2p->remote<detail::resolver_protocol>(
      peer, protocol,
      forge::plugins::p2p::node::remote_options{.open_deadline = open_deadline(options),
                                             .deadline = query_deadline(options)});
   auto result = co_await remote->query(query{});
   co_return std::move(result.apis);
}

} // namespace forge::plugins::p2p::resolver
