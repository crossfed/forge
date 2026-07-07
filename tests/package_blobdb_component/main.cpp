#include <cstddef>
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
   auto id = forge::blobdb::digest{std::vector<std::byte>{std::byte{1}}};
   auto reference = forge::blobdb::sha256_ref{.digest = forge::crypto::sha256::hash("package-blobdb-ref"), .size = 9};
   auto encoded = forge::variant{};
   forge::to_variant(reference, encoded);
   auto decoded = forge::blobdb::sha256_ref{};
   forge::from_variant(encoded, decoded);
   return id.empty() || decoded.size != reference.size || decoded.digest != reference.digest ? 1 : 0;
}
