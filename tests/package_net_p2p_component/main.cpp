import forge.net.p2p.dht;
import forge.net.p2p.dht.record_store;
import forge.net.p2p.address_resolution;
import forge.net.p2p.dialing;
import forge.net.p2p.identity;
import forge.net.p2p.ipns;
import forge.net.p2p.provider_registration;
import forge.net.p2p.topology;
import forge.net.p2p.node;
import forge.multiformats.multiaddr;

static_assert(requires(forge::net::p2p::node& node, forge::multiformats::multiaddr address) {
   node.async_connect(address);
   node.async_connect(address, forge::net::p2p::node::connect_options{});
});

int main() {
   const auto id = forge::net::p2p::peer_id{};
   auto store = forge::net::p2p::dht::record_store{
       forge::net::p2p::amino_v1(), {.persistence = forge::net::p2p::dht::record_store::make_memory_persistence()}};
   auto registration = forge::net::p2p::provider_registration{};
   const auto topology = forge::net::p2p::topology::policy{};
   const auto address_resolution = forge::net::p2p::address_resolution::policy{};
   const auto dialing = forge::net::p2p::dialing::black_hole_policy{};
   return id.value.empty() && !registration.active() && forge::net::p2p::ipns::routing_prefix.size() == 6 &&
                  !store.persistence_state().closed &&
                  topology.operating_mode == forge::net::p2p::topology::mode::managed &&
                  topology.peers.low == 128 && topology.peers.target == 160 && topology.peers.high == 192 &&
                  address_resolution.bounds.max_dns_lookups == 32 && address_resolution.bounds.max_txt_records == 16 &&
                  dialing.window_size == 100 && dialing.min_successes == 5 && dialing.udp_enabled && dialing.ipv6_enabled
              ? 0
              : 1;
}
