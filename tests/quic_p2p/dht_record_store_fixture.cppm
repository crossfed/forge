module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>

export module forge.test.net.p2p.dht_record_store_fixture;

import forge.net.p2p.dht.record_store;

export namespace forge::test::net::p2p {

class dht_record_store_persistence final : public forge::net::p2p::dht::record_store::persistence {
 public:
   dht_record_store_persistence();

   boost::asio::awaitable<forge::net::p2p::dht::record_store::hydration_page>
   async_hydrate(forge::net::p2p::dht::record_store::hydration_request request) override;
   boost::asio::awaitable<forge::net::p2p::dht::record_store::apply_result>
   async_apply(forge::net::p2p::dht::record_store::mutation_batch batch) override;
   boost::asio::awaitable<forge::net::p2p::dht::record_store::prune_result>
   async_prune_expired(std::chrono::system_clock::time_point now, std::size_t limit) override;
   boost::asio::awaitable<void> async_flush() override;
   boost::asio::awaitable<void> async_close() override;

   void fail_next_apply();
   void fail_next_flush();
   void fail_next_close();
   void make_next_apply_durability_uncertain();
   void make_next_prune_durability_uncertain();

 private:
   std::shared_ptr<forge::net::p2p::dht::record_store::persistence> inner_;
   std::mutex mutex_;
   bool fail_next_apply_ = false;
   bool fail_next_flush_ = false;
   bool fail_next_close_ = false;
   bool uncertain_next_apply_ = false;
   bool uncertain_next_prune_ = false;
};

} // namespace forge::test::net::p2p
