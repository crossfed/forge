module;

#include <forge/api/core/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.plugins.p2p.resolver.plugin;

import forge.api.core.binding;
import forge.api.transport.options;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;
import forge.plugins.p2p.resolver.types;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.types;

#include "details/plugin_impl.hxx"

namespace forge::plugins::p2p::resolver {

plugin::resolver_protocol_service::resolver_protocol_service(std::shared_ptr<plugin::impl> impl)
   : impl_{std::move(impl)} {}

boost::asio::awaitable<response> plugin::resolver_protocol_service::query(
   ::forge::plugins::p2p::resolver::query request) {
   co_return impl_->query_local(request);
}

} // namespace forge::plugins::p2p::resolver
