#include <boost/asio/awaitable.hpp>

#include <coroutine>

import forge.plugins.db.store.plugin;
import forge.db.revision.types;

boost::asio::awaitable<void>
use_revision_layer(forge::plugins::db::store::store_handle store) {
   auto active = co_await store.begin_transaction();
   const auto revision = co_await store.revisions().join(active);
   if (revision.id() == 0U) {
      co_await active.rollback();
      co_return;
   }
   co_await active.rollback();
}

int main() {
   const auto descriptor = forge::plugins::db::store::descriptor();
   const auto api = forge::plugins::db::store::api::describe();
   const auto options = forge::db::revision::prune_options{
      .max_revisions = 1U,
      .max_deltas = 1U,
   };
   return descriptor.id.value == "forge.plugins.db.store" &&
                 api.version.major == 1U && api.version.revision == 1U &&
                 options.max_revisions == 1U
             ? 0
             : 1;
}
