import forge.net.p2p.dht;
import forge.net.p2p.dht.record_store;
import forge.net.p2p.identity;
import forge.net.p2p.ipns;
import forge.net.p2p.provider_registration;

int main() {
   const auto id = forge::net::p2p::peer_id{};
   auto store = forge::net::p2p::dht::record_store{
       forge::net::p2p::amino_v1(), {.persistence = forge::net::p2p::dht::record_store::make_memory_persistence()}};
   auto registration = forge::net::p2p::provider_registration{};
   return id.value.empty() && !registration.active() && forge::net::p2p::ipns::routing_prefix.size() == 6 &&
                  !store.persistence_state().closed
              ? 0
              : 1;
}
