#include <cstddef>
#include <string>
#include <vector>

import forge.db.blob.ref;
import forge.db.blob.store;
import forge.db.blob.types;
import forge.crypto.sha256;
import forge.db.record;
import forge.variant.value;

int main() {
   auto cfg = forge::db::blob::store::config{};
   cfg.data_family = forge::db::family{"blobdb.data"};
   cfg.refs_family = forge::db::family{"blobdb.refs"};
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
   return decoded.size != reference.size || decoded.digest != reference.digest ? 1 : 0;
}
