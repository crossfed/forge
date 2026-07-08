module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>

#include <utility>
#include <vector>

export module forge.plugins.p2p.resolver.api;

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;
import forge.plugins.p2p.resolver.types;

export namespace forge::plugins::p2p::resolver {

class api : public forge::api::core::contract<api> {
 public:
   virtual ~api() = default;

   virtual void publish_api(forge::api::core::binding_plan plan, forge::net::p2p::protocol_id protocol,
                            publish_options options = {}) = 0;
   [[nodiscard]] virtual std::vector<entry> local_apis() const = 0;
   virtual boost::asio::awaitable<std::vector<entry>> peer_apis(forge::net::p2p::peer_id peer,
                                                                resolve_options options = {}) = 0;
   virtual boost::asio::awaitable<resolution> resolve(forge::net::p2p::peer_id peer, forge::api::core::api_ref api,
                                                      resolve_options options = {}) = 0;
   template <typename Interface>
   boost::asio::awaitable<forge::api::core::handle<Interface>> remote(forge::net::p2p::peer_id peer, resolve_options options = {}) {
      auto descriptor = Interface::describe();
      auto requested = forge::api::core::api_ref{.id = descriptor.id,
                                         .major = descriptor.version.major,
                                         .min_revision = descriptor.version.revision};
      auto resolved =
         co_await open_resolved_connection(std::move(peer), std::move(requested), std::move(descriptor), options);
      co_return co_await resolved.connection.template get_remote_api<Interface>(std::move(resolved.selected));
   }

 private:
   virtual boost::asio::awaitable<resolved_connection>
   open_resolved_connection(forge::net::p2p::peer_id peer, forge::api::core::api_ref api, forge::api::core::descriptor descriptor,
                            resolve_options options) = 0;
};

} // namespace forge::plugins::p2p::resolver

export {
FORGE_API(::forge::plugins::p2p::resolver::api, FORGE_API_CONTRACT("forge.plugins.p2p.resolver", 1, 0))
}
