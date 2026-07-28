module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.p2p.node.plugin;

import forge.api.transport.options;
import forge.asio.runtime;
import forge.asio.task;
import forge.net.p2p.diagnostics;
import forge.net.p2p.endpoint;
import forge.net.p2p.identity;
import forge.net.p2p.node;
import forge.net.p2p.protocol;
import forge.net.p2p.pubsub;
import forge.net.p2p.scoring;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.types;

#include "details/plugin_impl.hxx"
#include "details/diagnostics_source.hxx"

namespace forge::plugins::p2p::node {

plugin::diagnostics_source_adapter::diagnostics_source_adapter(std::shared_ptr<plugin::impl> impl)
    : impl_{std::move(impl)} {}

forge::net::p2p::diagnostics::snapshot
plugin::diagnostics_source_adapter::snapshot(forge::net::p2p::diagnostics::options options) const {
   return impl_->require_node().diagnostics(options);
}

} // namespace forge::plugins::p2p::node
