module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

module forge.test.net.p2p.dht_record_store_fixture;

namespace forge::test::net::p2p {

dht_record_store_persistence::dht_record_store_persistence()
    : inner_(forge::net::p2p::dht::record_store::make_memory_persistence()) {}

boost::asio::awaitable<forge::net::p2p::dht::record_store::hydration_page>
dht_record_store_persistence::async_hydrate(forge::net::p2p::dht::record_store::hydration_request request) {
   co_return co_await inner_->async_hydrate(std::move(request));
}

boost::asio::awaitable<forge::net::p2p::dht::record_store::apply_result>
dht_record_store_persistence::async_apply(forge::net::p2p::dht::record_store::mutation_batch batch) {
   auto uncertain = false;
   {
      auto lock = std::scoped_lock{mutex_};
      if (fail_next_apply_) {
         fail_next_apply_ = false;
         throw std::runtime_error{"injected DHT record persistence failure"};
      }
      uncertain = std::exchange(uncertain_next_apply_, false);
   }
   auto result = co_await inner_->async_apply(std::move(batch));
   if (uncertain) {
      result.durability_confirmed = false;
      result.durability_failure = "injected post-commit DHT durability failure";
   }
   co_return result;
}

boost::asio::awaitable<forge::net::p2p::dht::record_store::prune_result>
dht_record_store_persistence::async_prune_expired(std::chrono::system_clock::time_point now, std::size_t limit) {
   auto uncertain = false;
   {
      auto lock = std::scoped_lock{mutex_};
      uncertain = std::exchange(uncertain_next_prune_, false);
   }
   auto result = co_await inner_->async_prune_expired(now, limit);
   if (uncertain) {
      result.durability.durability_confirmed = false;
      result.durability.durability_failure = "injected post-commit DHT prune durability failure";
   }
   co_return result;
}

boost::asio::awaitable<void> dht_record_store_persistence::async_flush() {
   {
      auto lock = std::scoped_lock{mutex_};
      if (std::exchange(fail_next_flush_, false)) {
         throw std::runtime_error{"injected DHT record flush failure"};
      }
   }
   co_await inner_->async_flush();
}

boost::asio::awaitable<void> dht_record_store_persistence::async_close() {
   {
      auto lock = std::scoped_lock{mutex_};
      if (std::exchange(fail_next_close_, false)) {
         throw std::runtime_error{"injected DHT record close failure"};
      }
   }
   co_await inner_->async_close();
}

void dht_record_store_persistence::fail_next_apply() {
   auto lock = std::scoped_lock{mutex_};
   fail_next_apply_ = true;
}

void dht_record_store_persistence::fail_next_flush() {
   auto lock = std::scoped_lock{mutex_};
   fail_next_flush_ = true;
}

void dht_record_store_persistence::fail_next_close() {
   auto lock = std::scoped_lock{mutex_};
   fail_next_close_ = true;
}

void dht_record_store_persistence::make_next_apply_durability_uncertain() {
   auto lock = std::scoped_lock{mutex_};
   uncertain_next_apply_ = true;
}

void dht_record_store_persistence::make_next_prune_durability_uncertain() {
   auto lock = std::scoped_lock{mutex_};
   uncertain_next_prune_ = true;
}

} // namespace forge::test::net::p2p
