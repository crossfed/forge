module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.net.p2p.peer_store;

import forge.asio.gate;

#include "details/peer_store_impl.hxx"

namespace forge::net::p2p {

peer_store::persistence::~persistence() = default;

peer_store::peer_store() : peer_store{options{.persistence = make_memory_persistence()}} {}

peer_store::peer_store(options options_value) : impl_(std::make_shared<impl>(std::move(options_value))) {}

peer_store::~peer_store() = default;
peer_store::peer_store(peer_store&&) noexcept = default;
peer_store& peer_store::operator=(peer_store&&) noexcept = default;

void peer_store::upsert(record value) {
   impl_->upsert(std::move(value));
}

void peer_store::learn_endpoint(peer_id peer, forge::net::p2p::endpoint endpoint, capability_set capabilities) {
   impl_->learn_endpoint(std::move(peer), std::move(endpoint), capabilities);
}

void peer_store::mark_reachability(peer_id peer, reachability::state state,
                                   std::optional<forge::net::p2p::endpoint> observed) {
   impl_->mark_reachability(std::move(peer), state, std::move(observed));
}

void peer_store::mark_success(const peer_id& peer, path::kind kind, std::chrono::milliseconds latency) {
   impl_->mark_success(peer, kind, latency);
}

void peer_store::mark_failure(const peer_id& peer) {
   impl_->mark_failure(peer);
}

void peer_store::mark_endpoint_success(const peer_id& peer, const forge::net::p2p::endpoint& endpoint,
                                       path::kind kind, std::chrono::milliseconds latency) {
   impl_->mark_endpoint_success(peer, endpoint, kind, latency);
}

void peer_store::mark_endpoint_failure(const peer_id& peer, const forge::net::p2p::endpoint& endpoint,
                                       path::kind kind, std::chrono::system_clock::time_point backoff_until) {
   impl_->mark_endpoint_failure(peer, endpoint, kind, backoff_until);
}

void peer_store::upsert_routing_peer(dht::peer value, discovery::source source,
                                     std::chrono::system_clock::time_point expires_at) {
   impl_->upsert_routing_peer(std::move(value), source, expires_at);
}

boost::asio::awaitable<void> peer_store::async_upsert_provider(provider_record value) {
   co_await impl_->async_upsert_provider(std::move(value));
}

boost::asio::awaitable<void> peer_store::async_upsert_rendezvous(rendezvous::registration value) {
   co_await impl_->async_upsert_rendezvous(std::move(value));
}

boost::asio::awaitable<void> peer_store::async_remove_rendezvous(peer_id peer, std::string namespace_name) {
   co_await impl_->async_remove_rendezvous(std::move(peer), std::move(namespace_name));
}

boost::asio::awaitable<void> peer_store::async_hydrate() {
   co_await impl_->async_hydrate();
}

boost::asio::awaitable<peer_store::prune_result>
peer_store::async_prune_expired(std::chrono::system_clock::time_point now) {
   co_return co_await impl_->async_prune_expired(now);
}

boost::asio::awaitable<void> peer_store::async_flush() {
   co_await impl_->async_flush();
}

boost::asio::awaitable<void> peer_store::async_close() {
   co_await impl_->async_close();
}

std::optional<peer_store::record> peer_store::find(const peer_id& peer) const {
   return impl_->find(peer);
}

std::vector<peer_store::record> peer_store::snapshot(std::size_t limit) const {
   return impl_->snapshot(limit);
}

std::vector<peer_store::record> peer_store::candidates(std::uint64_t capability, std::size_t limit) const {
   return impl_->candidates(capability, limit);
}

std::vector<peer_store::provider_record> peer_store::find_providers(const dht::key& key,
                                                                    std::size_t limit) const {
   return impl_->find_providers(key, limit);
}

std::vector<rendezvous::registration>
peer_store::discover_rendezvous(std::string_view namespace_name, std::uint64_t after_sequence,
                                std::size_t limit) const {
   return impl_->discover_rendezvous(namespace_name, after_sequence, limit);
}

peer_store::persistence_status peer_store::persistence_state() const {
   return impl_->persistence_state();
}

} // namespace forge::net::p2p
