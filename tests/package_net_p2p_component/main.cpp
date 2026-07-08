import forge.net.p2p.identity;

int main() {
   const auto id = forge::net::p2p::peer_id{};
   return id.value.empty() ? 0 : 1;
}
