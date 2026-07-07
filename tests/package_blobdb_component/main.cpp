#include <cstddef>
#include <string>
#include <vector>

import forge.blobdb.ref;
import forge.blobdb.store;
import forge.blobdb.types;
import forge.crypto.sha256;
import forge.db.record;
import forge.variant.value;

int main() {
   auto cfg = forge::blobdb::store::config{};
   cfg.data_family = forge::db::family{"blobdb.data"};
   cfg.refs_family = forge::db::family{"blobdb.refs"};
   auto payload = std::string{"package-blobdb-ref"};
   auto bytes = std::vector<std::byte>{
      reinterpret_cast<const std::byte*>(payload.data()),
      reinterpret_cast<const std::byte*>(payload.data() + payload.size())};
   auto reference = forge::blobdb::sha256_ref{
      .digest = forge::blobdb::hash<forge::blobdb::digest>{}(bytes),
      .size = bytes.size()};
   auto encoded = forge::variant{};
   forge::to_variant(reference, encoded);
   auto decoded = forge::blobdb::sha256_ref{};
   forge::from_variant(encoded, decoded);
   return decoded.size != reference.size || decoded.digest != reference.digest ? 1 : 0;
}
