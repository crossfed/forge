module;

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

export module forge.blobdb.transaction;

import forge.blobdb.types;
import forge.db.record;
import forge.db.transaction;

export namespace forge::blobdb {

class transaction {
 public:
   transaction() = default;
   transaction(forge::db::transaction&& active,
               forge::db::family data_family,
               forge::db::family refs_family,
               std::shared_ptr<hasher> digest_hasher,
               bool verify_writes,
               bool verify_reads);
   transaction(forge::db::transaction& active,
               forge::db::family data_family,
               forge::db::family refs_family,
               std::shared_ptr<hasher> digest_hasher,
               bool verify_writes,
               bool verify_reads);

   [[nodiscard]] forge::db::transaction& db_transaction() const;

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

   boost::asio::awaitable<void> commit();
   boost::asio::awaitable<void> rollback();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::blobdb
