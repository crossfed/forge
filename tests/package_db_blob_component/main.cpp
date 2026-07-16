#include <cstddef>
#include <concepts>
#include <string>
#include <vector>

import forge.db.blob.ref;
import forge.db.blob.snapshot;
import forge.db.blob.store;
import forge.db.blob.types;
import forge.crypto.sha256;
import forge.db.core.record;
import forge.ids.object_id;
import forge.variant.value;

int main() {
   using owner_id = forge::ids::typed_id<7, 11>;
   static_assert(std::constructible_from<forge::db::blob::owner_ref, owner_id>);
   static_assert(!std::convertible_to<owner_id, forge::db::blob::owner_ref>);
   static_assert(requires(forge::db::blob::snapshot& view,
                          forge::db::blob::ref<> reference) {
      view.get(reference);
      view.has(reference);
      view.stat_blob(reference);
      view.verify(reference);
      view.ref_count(reference);
   });
   auto cfg = forge::db::blob::store::config{};
   cfg.data_family = forge::db::core::family{"blobdb.data"};
   cfg.refs_family = forge::db::core::family{"blobdb.refs"};
   auto payload = std::string{"package-db-blob-ref"};
   auto bytes = std::vector<std::byte>{
      reinterpret_cast<const std::byte*>(payload.data()),
      reinterpret_cast<const std::byte*>(payload.data() + payload.size())};
   auto reference = forge::db::blob::ref<forge::db::blob::digest>{
      .digest = forge::db::blob::hash<forge::db::blob::digest>{}(bytes),
      .size = bytes.size()};
   auto encoded = forge::variant{};
   forge::to_variant(reference, encoded);
   auto decoded = forge::db::blob::ref<forge::db::blob::digest>{};
   forge::from_variant(encoded, decoded);
   const auto typed_id = owner_id{42};
   const auto typed_owner = forge::db::blob::owner_ref{typed_id};
   const auto object_owner = forge::db::blob::owner_ref{typed_id.as_object_id()};
   return decoded.size != reference.size || decoded.digest != reference.digest ||
                 typed_owner.empty() || typed_owner != object_owner
            ? 1
            : 0;
}
