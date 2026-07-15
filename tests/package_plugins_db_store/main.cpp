#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <concepts>
#include <coroutine>
#include <cstdint>

import forge.plugins.db.store.plugin;
import forge.db.blob.snapshot;
import forge.db.object.index;
import forge.db.object.object;
import forge.db.revision.types;

struct usage_record : forge::db::object::object<usage_record, 1, 31> {
   std::uint64_t bytes = 0;
};

BOOST_DESCRIBE_STRUCT(usage_record,
                      (forge::db::object::object<usage_record, 1, 31>),
                      (bytes))

struct by_id;
struct by_bytes;

using usage_index = forge::db::object::object_index<
   usage_record,
   forge::db::object::indexed_by<forge::db::object::ranked_primary_unique<
      by_id,
      forge::db::object::ranked_schema<1>,
      forge::db::object::sum<
         by_bytes,
         forge::db::object::member<&usage_record::bytes>>>>>;

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

boost::asio::awaitable<void>
use_shared_read(forge::plugins::db::store::store_handle store) {
   auto read = co_await store.begin_read();
   if (read.active()) {
      static_cast<void>(read.objects());
      static_cast<void>(read.blobs());
   }
}

int main() {
   using object_handle = forge::plugins::db::store::object_handle;
   static_assert(requires(const object_handle& objects, const usage_record& value) {
      objects.index<usage_index, by_id>().count();
      objects.index<usage_index, by_id>().sum<by_bytes>();
      objects.index<usage_index, by_id>().nth(0U);
      objects.index<usage_index, by_id>().rank(value);
   });

   const auto descriptor = forge::plugins::db::store::descriptor();
   const auto api = forge::plugins::db::store::api::describe();
   const auto options = forge::db::revision::prune_options{
      .max_revisions = 1U,
      .max_deltas = 1U,
   };
   return descriptor.id.value == "forge.plugins.db.store" &&
                 api.version.major == 1U && api.version.revision == 2U &&
                 options.max_revisions == 1U
             ? 0
             : 1;
}
