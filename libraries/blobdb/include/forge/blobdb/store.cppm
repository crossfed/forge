module;

#include <boost/asio/awaitable.hpp>

#include <concepts>
#include <memory>
#include <vector>

export module forge.blobdb.store;

import forge.blobdb.transaction;
import forge.blobdb.types;
import forge.db.driver;
import forge.db.record;

export namespace forge::blobdb {

class store {
 public:
   struct config {
      forge::db::family data_family{"blobdb.data"};
      forge::db::family refs_family{"blobdb.refs"};
      std::shared_ptr<hasher> digest_hasher;
      bool verify_on_write = true;
      bool verify_on_read = true;
   };

   explicit store(std::shared_ptr<forge::db::driver> driver);
   store(std::shared_ptr<forge::db::driver> driver, config settings);

   boost::asio::awaitable<transaction> begin_transaction();
   [[nodiscard]] transaction join(forge::db::transaction& active);

   template <typename SharedTransaction>
      requires requires(SharedTransaction& active) {
         { active.db_transaction() } -> std::same_as<forge::db::transaction&>;
      }
   [[nodiscard]] transaction join(SharedTransaction& active) {
      return join(active.db_transaction());
   }

   boost::asio::awaitable<digest> put(std::vector<std::byte> bytes);
   boost::asio::awaitable<void> put(digest id, std::vector<std::byte> bytes);
   boost::asio::awaitable<std::vector<std::byte>> get(digest id);
   boost::asio::awaitable<bool> has(digest id);
   boost::asio::awaitable<stat> stat_blob(digest id);
   boost::asio::awaitable<void> erase(digest id);
   boost::asio::awaitable<void> verify(digest id);
   boost::asio::awaitable<void> retain(digest id, owner_ref owner);
   boost::asio::awaitable<void> release(digest id, owner_ref owner);
   boost::asio::awaitable<std::uint64_t> ref_count(digest id);
   boost::asio::awaitable<collect_result> collect_unreferenced(collect_options options = {});

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::blobdb
