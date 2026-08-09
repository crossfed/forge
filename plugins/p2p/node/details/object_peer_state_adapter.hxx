#pragma once

namespace forge::plugins::p2p::node {

class object_peer_state_adapter final : public forge::net::p2p::peer_store::persistence {
 public:
   static void register_schema(const forge::plugins::db::store::store_handle& store);
   [[nodiscard]] static boost::asio::awaitable<std::shared_ptr<object_peer_state_adapter>>
   async_open(forge::plugins::db::store::api* db, forge::plugins::db::store::store_handle store);

   boost::asio::awaitable<forge::net::p2p::peer_store::hydration_page>
   async_hydrate(forge::net::p2p::peer_store::hydration_request request) override;
   boost::asio::awaitable<void> async_apply(forge::net::p2p::peer_store::mutation_batch batch) override;
   boost::asio::awaitable<forge::net::p2p::peer_store::prune_result>
   async_prune_expired(std::chrono::system_clock::time_point now, std::size_t limit) override;
   boost::asio::awaitable<void> async_flush() override;
   boost::asio::awaitable<void> async_close() override;

 private:
   object_peer_state_adapter(forge::plugins::db::store::api* db, forge::plugins::db::store::store_handle store);

   void ensure_open() const;

   forge::plugins::db::store::api* db_ = nullptr;
   forge::plugins::db::store::store_handle store_;
   std::atomic_bool closed_{false};
   std::atomic_uint8_t next_prune_kind_{0};
};

} // namespace forge::plugins::p2p::node
