module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

module forge.net.p2p.dht.record_store;

import forge.asio.gate;

#include "details/dht_record_store_impl.hxx"
#include "details/memory_dht_record_store_persistence.hxx"

namespace forge::net::p2p {

dht::record_store::persistence::~persistence() = default;

dht::record_store::record_store(dht::profile profile) : record_store{std::move(profile), options{}} {}

dht::record_store::record_store(dht::profile profile, options options_value)
    : impl_(std::make_shared<impl>(std::move(profile), std::move(options_value))) {}

dht::record_store::~record_store() = default;
dht::record_store::record_store(record_store&&) noexcept = default;
dht::record_store& dht::record_store::operator=(record_store&&) noexcept = default;

std::shared_ptr<dht::record_store::persistence> dht::record_store::make_memory_persistence() {
   return make_memory_dht_record_store_persistence();
}

boost::asio::awaitable<dht::record_store::put_result>
dht::record_store::async_put(value_record incoming, std::chrono::system_clock::time_point now) {
   return impl::async_put_owned(impl_, std::move(incoming), now);
}

boost::asio::awaitable<std::optional<dht::record_store::put_result>>
dht::record_store::async_put_received(value_record incoming, std::chrono::system_clock::time_point now) {
   return impl::async_put_received_owned(impl_, std::move(incoming), now);
}

boost::asio::awaitable<void> dht::record_store::async_upsert_provider(provider_record value,
                                                                      std::chrono::system_clock::time_point now) {
   return impl::async_upsert_provider_owned(impl_, std::move(value), now);
}

boost::asio::awaitable<void> dht::record_store::async_remove_provider(provider_key key) {
   return impl::async_remove_provider_owned(impl_, std::move(key));
}

boost::asio::awaitable<void> dht::record_store::async_hydrate(std::chrono::system_clock::time_point now) {
   return impl::async_hydrate_owned(impl_, now);
}

boost::asio::awaitable<dht::record_store::prune_result>
dht::record_store::async_prune_expired(std::chrono::system_clock::time_point now) {
   return impl::async_prune_expired_owned(impl_, now);
}

boost::asio::awaitable<void> dht::record_store::async_flush() {
   return impl::async_flush_owned(impl_);
}

boost::asio::awaitable<void> dht::record_store::async_close() {
   return impl::async_close_owned(impl_);
}

std::optional<dht::record_store::value_record>
dht::record_store::find_value(const dht::key& key, std::chrono::system_clock::time_point now) const {
   return impl_->find_value(key, now);
}

std::vector<dht::record_store::provider_record>
dht::record_store::find_providers(const dht::key& key, std::size_t limit,
                                  std::chrono::system_clock::time_point now) const {
   return impl_->find_providers(key, limit, now);
}

dht::record_store::persistence_status dht::record_store::persistence_state() const {
   return impl_->persistence_state();
}

} // namespace forge::net::p2p
