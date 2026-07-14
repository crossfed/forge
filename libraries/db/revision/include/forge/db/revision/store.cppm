module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module forge.db.revision.store;

import forge.db.core.driver;
import forge.db.object.store;
import forge.db.revision.transaction;
import forge.db.revision.types;

export namespace forge::db::revision {

class store {
 public:
   static boost::asio::awaitable<store>
   open(std::shared_ptr<forge::db::core::driver> driver,
        forge::db::object::store objects);

   boost::asio::awaitable<scope> join(forge::db::core::transaction& active);
   boost::asio::awaitable<transaction> begin_transaction();

   boost::asio::awaitable<void>
   revert(forge::db::core::transaction& active, revision_id_t expected_head);

   boost::asio::awaitable<prune_result>
   prune_through(forge::db::core::transaction& active,
                 revision_id_t inclusive_boundary,
                 prune_options options);

 private:
   struct impl;
   explicit store(std::shared_ptr<impl> implementation);

   std::shared_ptr<impl> impl_;
};

} // namespace forge::db::revision
