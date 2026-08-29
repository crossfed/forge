import forge.plugins.crypto.secrets.descriptor;
import forge.plugins.crypto.signer.descriptor;
import forge.plugins.db.store.descriptor;
import forge.plugins.http.server.descriptor;
import forge.plugins.p2p.node.descriptor;
import forge.plugins.p2p.pubsub.descriptor;
import forge.plugins.p2p.resolver.descriptor;

int main() {
   static_cast<void>(forge::plugins::crypto::secrets::default_descriptor());
   static_cast<void>(forge::plugins::crypto::signer::default_descriptor());
   static_cast<void>(forge::plugins::db::store::default_descriptor());
   static_cast<void>(forge::plugins::http::server::default_descriptor());
   static_cast<void>(forge::plugins::p2p::node::default_descriptor());
   static_cast<void>(forge::plugins::p2p::pubsub::default_descriptor());
   static_cast<void>(forge::plugins::p2p::resolver::default_descriptor());
   return 0;
}
